/**
 * This wrapper integrates SVF's alias analysis results into LLVM passes,
 * providing alias set queries by parsing WPA output.
 */

#include "Alias/SVFAA/SVFWrapper.h"

#include <llvm/IR/Function.h>
#include <llvm/IR/Argument.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instruction.h>
#include <llvm/Bitcode/BitcodeWriter.h>

#include <sstream>
#include <iostream>
#include <memory>
#include <array>
#include <cstdio>
#include <algorithm>
#include <cctype>

using namespace llvm;

static cl::opt<std::string> WpaPath("svf-wpa-path", cl::init("/usr/bin"), cl::Hidden,
  cl::desc("Path to SVF's wpa executable"));

static cl::opt<std::string> TempDirectory("svf-wpa-temp-dir", cl::init("/tmp"), cl::Hidden,
  cl::desc("Temporary directory for bitcode files"));

static cl::opt<bool> PrintAliasInfo("svf-print-alias-info", cl::init(true), cl::Hidden,
  cl::desc("Print alias sets and evaluation results"));

static cl::opt<std::string> SVFAliasAnalysisType("svf-aatype", cl::init("/tmp"), cl::Hidden,
  cl::desc("Temporary directory for bitcode files"));

#define EXIT(msg) {llvm::outs()<<msg<<"\n";exit(-1);}

char SVFWrapper::ID = 0;
static RegisterPass<SVFWrapper> X("svfwrapper","SVF Alias Analysis Wrapper");

std::size_t SVFWrapper::ValuePairHash::operator()(const std::pair<Value*, Value*>& p) const {
  return std::hash<Value*>{}(p.first) ^ (std::hash<Value*>{}(p.second) << 1);
}
SVFWrapper::SVFWrapper() : ModulePass(ID), CurrentModule(nullptr), svfaatype(SVFAliasAnalysisType) {}
SVFWrapper::SVFWrapper(SVFAAType svfaatype_) : ModulePass(ID), CurrentModule(nullptr), svfaatype(svfaatype_) {}
SVFWrapper::~SVFWrapper() {}
// a series of tedious CLI operations
bool SVFWrapper::runOnModule(Module& M) {
  CurrentModule = &M;
  std::string BcPath = TempDirectory + "/svf_temp_" + std::to_string(reinterpret_cast<uintptr_t>(CurrentModule)) + ".bc";
  std::error_code EC;
  raw_fd_ostream OS(BcPath, EC);
  if (EC) EXIT("Cannot create temp file: " + EC.message())
  WriteBitcodeToFile(*CurrentModule, OS);
  OS.flush();
  std::string Cmd = "cd " + WpaPath + " && ./wpa" + " -print-aliases -" + svfaatype + " " + BcPath;
  if (PrintAliasInfo) outs() << "Executing: " << Cmd << "\n";
  std::array<char, 128> Buffer;
  std::string Result;
  FILE* Pipe = popen(Cmd.c_str(), "r");
  if (!Pipe) EXIT("popen() failed")
  while (fgets(Buffer.data(), Buffer.size(), Pipe) != nullptr) Result += Buffer.data();
  int ExitCode = pclose(Pipe);
  if (ExitCode != 0) EXIT("Command failed (exit code " + std::to_string(ExitCode) + "): " + Result)
  parseOutput(Result);
  std::remove(BcPath.c_str());
  if (PrintAliasInfo) outs() << "SVFWrapper found " << AliasMap.size() << " alias pairs.\n\n";  
  return false;
}

void SVFWrapper::getAnalysisUsage(AnalysisUsage& AU) const {
  AU.setPreservesAll();
}
// check alias
llvm::AliasResult SVFWrapper::alias(const llvm::MemoryLocation& LocA,
                                    const llvm::MemoryLocation& LocB) {
  llvm::Value* PtrA = const_cast<llvm::Value*>(LocA.Ptr);
  llvm::Value* PtrB = const_cast<llvm::Value*>(LocB.Ptr);
  if (PtrA == PtrB) return llvm::AliasResult::MustAlias;
  auto It = AliasMap.find({PtrA, PtrB});
  if (It != AliasMap.end()) {
    return It->second;
  }
  return llvm::AliasResult::NoAlias; // NoAlias iff not in SVF log
}
// find llvm::value* by function name and location info 
Value* SVFWrapper::findValue(const std::string& FuncName, const std::string& ValueStr) {
  if (FuncName == "<global>") {
    return CurrentModule->getGlobalVariable(ValueStr);
  }
  Function* Func = CurrentModule->getFunction(FuncName);
  if (!Func) return nullptr;
  if (ValueStr.find("arg") == 0) {
    int Idx = std::stoi(ValueStr.substr(3));
    if (Idx < (int)Func->arg_size()) {
      auto It = Func->arg_begin();
      std::advance(It, Idx);
      return &*It;
    }
    return nullptr;
  }
  if (std::all_of(ValueStr.begin(), ValueStr.end(), ::isdigit)) {
    int Idx = std::stoi(ValueStr);
    int Count = 0;
    for (auto& BB : *Func) {
      for (auto& Inst : BB) {
        if (Count++ == Idx) return &Inst;
      }
    }
  }
  return nullptr;
}
// parse the SVFAA log to get a alias pair represented by llvm::value
void SVFWrapper::parseOutput(const std::string& Output) {
  // Log Format: AliasResult,id1,name1,func1,module1:func1:loc1,id2,name2,func2,module2:func2:loc2
  std::istringstream Iss(Output);
  std::string Line;
  bool InSection = false;
  while (std::getline(Iss, Line)) {
    if (!InSection&&Line.find("PrintAliasPairs") != std::string::npos) {
      InSection = true;
      continue;
    }
    if (!InSection) continue;
    std::vector<std::string> Fields;
    std::stringstream Ss(Line);
    std::string Field;
    while (std::getline(Ss, Field, ',')) {
      Fields.push_back(Field);
    }
    if (Fields.size() != 9) continue;
    AliasResult Type = AliasResult::NoAlias;
    if (Fields[0] == "MayAlias") Type = AliasResult::MayAlias;
    else if (Fields[0] == "MustAlias") Type = AliasResult::MustAlias;
    else if (Fields[0] == "PartialAlias") Type = AliasResult::PartialAlias;
    std::string Info1 = Fields[4];
    std::string Info2 = Fields[8];
    std::vector<std::string> Parts1, Parts2;
    std::stringstream Ss1(Info1), Ss2(Info2);
    std::string P;
    while (std::getline(Ss1, P, ':')) Parts1.push_back(P);
    while (std::getline(Ss2, P, ':')) Parts2.push_back(P);
    if (Parts1.size() < 3 || Parts2.size() < 3) continue;
    Value* V1 = findValue(Parts1[1], Parts1[2]);
    Value* V2 = findValue(Parts2[1], Parts2[2]);
    if (V1 && V2) {
      AliasMap.emplace(std::make_pair(V1, V2), Type);
      AliasMap.emplace(std::make_pair(V2, V1), Type);
    }
  }
}