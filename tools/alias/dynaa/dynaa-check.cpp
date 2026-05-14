#include "Alias/Dynamic/DynamicAliasAnalysis.h"
#include "Alias/Dynamic/IDAssigner.h"
#include "Alias/Infrastructure/AliasAnalysisWrapper/AliasAnalysisWrapper.h"


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
  outs() << "[log] [dynaa-check] bc file ok." << "\n";

  // Perform dynamic alias analysis and get all DidAlias pairs
  DynamicAliasAnalysis dynAA(*module, LogFilename.data());
  dynAA.runAnalysis();
  outs() << "[log] [dynaa-check] dynAA finished parsing logs." << "\n";

  return 0;
}