#pragma once

#include "Alias/Infrastructure/AliasAnalysisWrapper/AliasRecord.h"
#include "Alias/Dynamic/AliasPair.h"
#include "Alias/Dynamic/DynamicPointer.h"
#include "Alias/Dynamic/IDAssigner.h"
#include "IR/ICFG/CallGraph.h"

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/DenseSet.h>
#include <llvm/Analysis/AliasAnalysis.h>

namespace dynamic {

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
  std::vector<AliasRecord> getAliasPairs() const{
    std::vector<AliasRecord> res;
    for (const auto &F:M) {
      auto itr = aliasPairMap.find(&F);
      if (itr == aliasPairMap.end()) continue;
      for (auto &record:itr->second) {
        if (!record.getFirst() || !record.getSecond()) continue;
        res.push_back(AliasRecord(record.getFirst(), record.getSecond(), llvm::AliasResult::MustAlias));
      }
    }
    return res;
  }
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
