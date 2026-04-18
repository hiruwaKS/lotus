#include "Alias/Dynamic/DynamicAliasAnalysis.h"
#include "Alias/Dynamic/IDAssigner.h"
#include "Alias/AliasAnalysisWrapper/AliasAnalysisWrapper.h"

#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/BasicAliasAnalysis.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>

using namespace dynamic;
using namespace llvm;


cl::opt<std::string> InputFilename(cl::Positional, cl::desc("<bitcode file>"));
cl::opt<std::string> LogFilename(cl::Positional, cl::desc("<log file>"));
cl::opt<std::string>
    AA(cl::Positional, cl::desc("<alias-analysis>"),"basic-aa");

bool checkAAResult(const std::string& name,std::unique_ptr<lotus::AliasAnalysisWrapper> aaResult, const DenseSet<AliasPair>& aliasSet,
                   int& must_cnt, int& may_cnt) {
    for (auto const& pair : aliasSet) {
        const auto *valA = pair.getFirst();
        const auto *valB = pair.getSecond();
        if (valA == nullptr || valB == nullptr)
            continue;
        // Create MemoryLocation objects from the values - use MemoryLocation's static method
        auto aliasResult = aaResult->mayAlias(valA, valB);
        
        if (!aliasResult) { // 
            outs() << "\nFIND AA BUG:\n";
            outs() << "  ValA = " << *valA << '\n';
            outs() << "  ValB = " << *valB << '\n';
            outs() << "  DynamicAA said DidAlias but the \"" << name << "\" said "
                      "NoAlias\n";
            return false;
        }
        if(aaResult->mustAlias(valA, valB)) ++must_cnt; else ++may_cnt;
    }
    return true;
}

int main(int argc, char **argv) {
  cl::ParseCommandLineOptions(argc, argv);

  LLVMContext context;
  SMDiagnostic error;
  auto module = parseIRFile(InputFilename, error, context);
  if (!module) {
    error.print(InputFilename.data(), errs());
    return -1;
  }

  // Perform dynamic alias analysis and get all DidAlias pairs
  DynamicAliasAnalysis dynAA(*module, LogFilename.data());
  dynAA.runAnalysis();

  // Set up aa pipeline
  FunctionAnalysisManager funManager;
  ModuleAnalysisManager modManager;

  // Register target library info
  TargetLibraryAnalysis TLI;
  int must_cnt=0, may_cnt=0;
  funManager.registerPass([&] { return TLI; });
    for (const auto& f : *module) {
        if (const auto *aliasSet = dynAA.getAliasPairs(&f)) {
            auto AAWrapper=lotus::AliasAnalysisFactory::create(*module,
                lotus::parseAAConfigFromString(AA,lotus::AAConfig::BasicAA()));
            if(!checkAAResult(AA, std::move(AAWrapper), *aliasSet,must_cnt,may_cnt)) {
                outs() << "[dynaa-check] Info: "<< 
                right_justify(AA, 15) << ", not sound" << "\n";
                return 0;
            }
        }
    }
    outs() << "[dynaa-check] Info: "<< 
        right_justify(AA, 15) << ", "<< 
        "precision=" << format("%.3f", 1.0*must_cnt/(must_cnt+may_cnt))<< ", "<<
        "pair_cnt=" << must_cnt+may_cnt << ", " <<
        "may=" << may_cnt << ", " <<
        "must=" << must_cnt << "\n";
    return 0;
}