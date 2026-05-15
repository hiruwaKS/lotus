/*
 * CclyzerAA Pointer Analysis Driver
 * Datalog-based pointer analysis using cclyzer++ backend.
 */

#include "Alias/InclusionBased/CclyzerAA/CclyzerAA.h"
#include "Alias/Infrastructure/AliasAnalysisWrapper/CLIUtils.h"
#include <llvm/ADT/Statistic.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/raw_ostream.h>


using namespace llvm;
using namespace lotus::alias::tools;

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input bitcode file>"),
                                          cl::Required);

static cl::opt<bool> PrintCallGraph("print-cg",
                                    cl::desc("Print call graph"),
                                    cl::init(false));

static cl::opt<bool> Verbose("v", cl::desc("Verbose output"), cl::init(false));
static cl::opt<bool> OnlyStatistics("s", cl::desc("Only output statistics"),
                                    cl::init(false));

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);
  cl::ParseCommandLineOptions(argc, argv, "CclyzerAA Pointer Analysis Tool\n");

  LLVMContext Context;
  SMDiagnostic Err;

  auto M = loadIRModule(InputFilename, Context, Err, argv[0]);
  if (!M) {
    return 1;
  }

  if (Verbose) {
    errs() << "Running CclyzerAA on " << M->getName() << " ("
           << M->getFunctionList().size() << " functions)\n";
  }

  // Run CclyzerAA analysis
  lotus::cclyzer::CclyzerAA CclyzerAA;
  if (!CclyzerAA.run(*M)) {
    errs() << "Error: CclyzerAA analysis failed\n";
    return 1;
  }

  // Print call graph statistics if requested
  if (PrintCallGraph && !OnlyStatistics) {
    // Print basic module-level call information instead.
    const auto& callgraph = CclyzerAA.getCallGraph();
    const auto& ctx_to_str = CclyzerAA.getContextToString();

    outs() << "Call graph (" << callgraph.size() << " edges):\n";
    for (const auto& [caller, callee_info] : callgraph) {
      auto [caller_ctx, callee_ctx, callee] = callee_info;
      if (auto *callerInst = dyn_cast<Instruction>(caller)) {
        outs() << callerInst->getParent()->getParent()->getName()
              << " -> " << callee->getName() << "\n";
      }
    }
  }

  if (OnlyStatistics || Verbose) {
    errs() << "\n=== Statistics ===\n";
    PrintStatistics(errs());
  }

  return 0;
}
