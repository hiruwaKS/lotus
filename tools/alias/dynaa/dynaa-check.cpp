#include "Alias/Dynamic/DynamicAliasAnalysis.h"
#include "Alias/Dynamic/IDAssigner.h"
#include "Alias/AliasAnalysisWrapper/AliasAnalysisWrapper.h"
#include "IR/ICFG/CallGraph.h"

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
  auto aliasPairs = dynAA.getAliasPairs();

  // Set up aa pipeline
  FunctionAnalysisManager funManager;
  ModuleAnalysisManager modManager;

  // Register target library info
  TargetLibraryAnalysis TLI;
  funManager.registerPass([&] { return TLI; });

  int no_cnt=0, must_cnt=0, may_cnt=0, dyn_calls=0, dyn_not_static=0, static_not_dyn=0;
  auto indirectCSs=LTCallGraph::getIndirectCallSites(*module);
  auto AAWrapper=lotus::AliasAnalysisFactory::create(*module,
                lotus::parseAAConfigFromString(AA,lotus::AAConfig::BasicAA()));
  for (auto &pair: aliasPairs) {
    const auto *valA = pair.getFirst();
    const auto *valB = pair.getSecond();
    // Create MemoryLocation objects from the values - use MemoryLocation's static method
    auto aliasResult = AAWrapper->mayAlias(valA, valB);
    if (aliasResult == llvm::AliasResult::NoAlias) ++no_cnt;
    // if (aliasResult == llvm::AliasResult::NoAlias) {
    //     outs() << "\nFIND AA BUG:\n";
    //     outs() << "  ValA = " << *valA << '\n';
    //     outs() << "  ValB = " << *valB << '\n';
    //     outs() << "  DynamicAA said DidAlias but the \"" << AA << "\" said "
    //               "NoAlias\n";
    //     break;
    // }
    if(AAWrapper->mustAlias(valA, valB)) ++must_cnt; else ++may_cnt;
  }
  std::vector<const Function*> dyn_callees, static_callees;
  for (auto &cs: indirectCSs) {
    dynAA.getCallGraph().getCallTargets(cs, dyn_callees);
    dyn_calls+=dyn_callees.size();
    AAWrapper->getIndirectCallTargets(cs,static_callees);
    auto count_difference = [](auto &vec1, auto &vec2, int& missing) {
      for (auto *v1 : vec1) if (std::find(vec2.begin(), vec2.end(), v1) == vec2.end()) missing++;
    };
    count_difference(dyn_callees, static_callees, dyn_not_static);
    count_difference(static_callees, dyn_callees, static_not_dyn);
  }
    outs() << "[dynaa-check] Info: "<< 
        right_justify(AA, 15) << ", "<< 
        "dynAliasPair" << "=" << aliasPairs.size() << ", " <<
        "unsound=" << no_cnt << ", " <<
        "may=" << may_cnt << ", " <<
        "must=" << must_cnt << ", " <<
        "indirectCS=" << indirectCSs.size() << ", " <<
        "dyn=" << dyn_calls << ", " <<
        "dyn/static=" << dyn_not_static << ", " <<
        "static/dyn=" << static_not_dyn <<
        "\n";
    return 0;
}