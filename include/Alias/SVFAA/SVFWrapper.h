/**
 * This wrapper integrates SVF's alias analysis results into LLVM passes,
 * providing alias set queries by parsing WPA output.
 */

#ifndef SVFWRAPPER_H
#define SVFWRAPPER_H

#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>
#include <llvm/Pass.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Analysis/AliasAnalysis.h>
#include <unordered_map>
#include <string>
#include <set>
#include <memory>

/// @brief SVF alias analysis wrapper
///
/// Runs SVF's CFLAnders or CFLSteens analysis on a module and provides
/// alias set queries and may-alias checks by parsing WPA output.

class SVFWrapper : public llvm::ModulePass {
private:
  struct ValuePairHash{
    std::size_t operator()(const std::pair<llvm::Value*, llvm::Value*>& p) const;
  };
  llvm::Module* CurrentModule;
  
  /// Alias result mapping
  std::unordered_map<std::pair<llvm::Value*, llvm::Value*>, 
                     llvm::AliasResult, 
                     ValuePairHash> AliasMap;

public:
  typedef std::string SVFAAType;
  static char ID;
  SVFAAType svfaatype;

  SVFWrapper();
  SVFWrapper(SVFAAType svfaatype);
  ~SVFWrapper() override;

  /// @brief Run the analysis on a module
  /// @param M The module to analyze
  /// @return true if analysis completed
  bool runOnModule(llvm::Module& M) override;

  void getAnalysisUsage(llvm::AnalysisUsage& AU) const override;

  llvm::AliasResult alias(const llvm::MemoryLocation& LocA, 
                          const llvm::MemoryLocation& LocB);

private:
  llvm::Value* findValue(const std::string& FuncName, const std::string& ValueStr);
  std::string execAndCapture(const std::string& Cmd);
  void parseOutput(const std::string& Output);
};

#endif // SVFWRAPPER_H