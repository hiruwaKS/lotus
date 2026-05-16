/*
 * CFL Alias Analysis Driver
 * Supports CFLAnders and CFLSteens analyses.
 * Builds call graph by printing (caller, callee) pairs.
 */

#include "Alias/Infrastructure/AliasAnalysisWrapper/CLIUtils.h"

#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/Statistic.h>
#include <llvm/Analysis/CFLAndersAliasAnalysis.h>
#include <llvm/Analysis/CFLSteensAliasAnalysis.h>
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/InitializePasses.h>
#include <llvm/PassRegistry.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;
using namespace lotus::alias::tools;

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input bitcode file>"),
                                          cl::Required);

static cl::opt<std::string> AnalysisType("type",
                                         cl::desc("Analysis type: CFLAnders or CFLSteens"),
                                         cl::init("CFLAnders"));

static cl::opt<bool> Verbose("v", cl::desc("Verbose output"), cl::init(false));
static cl::opt<bool> OnlyStatistics("s", cl::desc("Only output statistics"),
                                    cl::init(false));

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);
  cl::ParseCommandLineOptions(argc, argv, "CFL Alias Analysis Tool\n");

  // Initialize the Pass Registries
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeCore(PR);
  initializeAnalysis(PR);
  initializeCFLSteensAAWrapperPassPass(PR);
  initializeCFLAndersAAWrapperPassPass(PR);

  LLVMContext Context;
  SMDiagnostic Err;

  auto M = loadIRModule(InputFilename, Context, Err, argv[0]);
  if (!M) {
    return 1;
  }

  if (Verbose) {
    errs() << "Running " << AnalysisType << " on " << M->getName() << " ("
           << M->getFunctionList().size() << " functions)\n";
  }

  // Configure Pipeline
  legacy::PassManager PM;

  if (AnalysisType == "CFLAnders") {
    PM.add(createCFLAndersAAWrapperPass());
  } else if (AnalysisType == "CFLSteens") {
    PM.add(createCFLSteensAAWrapperPass());
  } else {
    errs() << "Unknown analysis type: " << AnalysisType
           << ". Use CFLAnders or CFLSteens.\n";
    return 1;
  }

  // PM takes ownership of this pointer.
  auto *AAWrapper = new AAResultsWrapperPass();
  PM.add(AAWrapper);
  
  // Execute the passes
  PM.run(*M);
  
  // Safe to get results here right after execution before PM scope ends
  AAResults &AA = AAWrapper->getAAResults();

  // Map potential target functions
  SmallPtrSet<const Function*, 32> Functions;
  for (auto &F : *M) {
    if (!F.isDeclaration()) {
      Functions.insert(&F);
    }
  }

  unsigned TotalIndirectCalls = 0;
  unsigned TotalResolvedTargets = 0;

  // Process indirect call sites
  for (auto &F : *M) {
    if (F.isDeclaration())
      continue;

    for (auto &BB : F) {
      for (auto &I : BB) {
        auto *CB = dyn_cast<CallBase>(&I);
        if (!CB)
          continue;

        // Skip direct calls
        if (CB->getCalledFunction())
          continue;

        TotalIndirectCalls++;
        Value *CalleeValue = CB->getCalledOperand();

        for (const Function *TargetFunc : Functions) {
          // Cast the function pointer to a generic PointerType (Opaque pointers)
          Constant *FuncPtr = ConstantExpr::getPointerCast(
              const_cast<Function*>(TargetFunc),
              PointerType::getUnqual(Context));

          AliasResult AR = AA.alias(CalleeValue, FuncPtr);

          if (AR != AliasResult::NoAlias) {
            if (!OnlyStatistics) {
              outs() << F.getName() << " -> " << TargetFunc->getName() << "\n";
            }
            TotalResolvedTargets++;
          }
        }
      }
    }
  }

  outs() << "\nCall graph statistics:\n"
         << "  Indirect calls: " << TotalIndirectCalls << "\n"
         << "  Resolved targets: " << TotalResolvedTargets << "\n";

  if (OnlyStatistics || Verbose) {
    errs() << "\n=== Statistics ===\n";
    PrintStatistics(errs());
  }

  return 0;
}