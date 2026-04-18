#pragma once

#include "Alias/Dynamic/AliasPair.h"
#include "Alias/Dynamic/DynamicPointer.h"
#include "Alias/Dynamic/IDAssigner.h"
#include "IR/ICFG/CallGraph.h"

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/DenseSet.h>
#include <llvm/Analysis/AliasAnalysis.h>

namespace dynamic {

/// @brief Log parsing to provide query for alias and indirect call
///
/// Queries can be performed using DynamicPointer (an index) or LLVM objects.
class DynamicAliasAnalysis {
private:
  using AliasPairSet = llvm::DenseSet<AliasPair>;
  using AnalysisMap = llvm::DenseMap<const llvm::Function *, AliasPairSet>;
  AnalysisMap aliasPairMap;

  const char *fileName;

  llvm::Module &M;
  LTCallGraph callGraph;
  IDAssigner idAssigner;

public:
  using const_iterator = AnalysisMap::const_iterator;

  DynamicAliasAnalysis(llvm::Module &M, const char* fileName);

  void runAnalysis();

  const AliasPairSet *getAliasPairs(const llvm::Function *) const;
  llvm::AliasResult alias(const llvm::MemoryLocation &LocA, const llvm::MemoryLocation &LocB) {
    if (auto InstA = llvm::dyn_cast<llvm::Instruction>(LocA.Ptr)) {
      if (auto InstB = llvm::dyn_cast<llvm::Instruction>(LocB.Ptr)) {
        auto pair = AliasPair(InstA, InstB);
        for (const auto func_pairs : aliasPairMap)
          if (func_pairs.second.contains(pair)) return llvm::AliasResult::MustAlias;
      }
    }
    return llvm::AliasResult::NoAlias;
  }
  const LTCallGraph &getCallGraph() { return callGraph; }

  const_iterator begin() const { return aliasPairMap.begin(); }
  const_iterator end() const { return aliasPairMap.end(); }
};
} // namespace dynamic
