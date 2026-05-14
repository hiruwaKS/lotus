#include "Alias/Dynamic/DynamicAliasAnalysis.h"

#include "Alias/Dynamic/AllocType.h"
#include "Alias/Dynamic/LogProcessor.h"

#include <llvm/ADT/SmallPtrSet.h>

using namespace llvm;

namespace dynamic {

namespace {

/// Processes execution logs to identify alias pairs between pointers.
/// Maintains per-function point-to sets and detects when pointers alias
/// by checking for overlapping target addresses.
class AnalysisImpl : public LogProcessor<AnalysisImpl> {
private:
  using AliasPairSet = DenseSet<AliasPair>;
  using AnalysisMap = DenseMap<const llvm::Function *, AliasPairSet>;
  AnalysisMap &aliasPairMap; ///< Maps functions to their discovered alias pairs

  using GlobalMap = DenseMap<const llvm::GlobalValue *, const void *>;
  GlobalMap globalMap; ///< Maps global pointer IDs to their addresses

  using PtsSet = SmallPtrSet<const void *, 4>;
  using LocalMap = DenseMap<const llvm::Value *, PtsSet>;
  /// Represents a function call frame with its local pointer mappings
  struct Frame {
    const llvm::Function * func; ///< Function identifier
    LocalMap localMap;   ///< Maps local pointer IDs to their point-to sets
  };
  std::vector<Frame> stackFrames; ///< Call stack for tracking nested functions
  const llvm::Instruction *currentCS; ///< The current callsite for building call graph
  IDAssigner &idAssigner;
  LTCallGraph &callGraph; ///< passed by the wrapper

  static bool intersects(const PtsSet &, const PtsSet &);
  void findAliasPairs();

public:
  AnalysisImpl(const char *fileName, AnalysisMap &m, IDAssigner &ida, LTCallGraph &cg)
      : LogProcessor<AnalysisImpl>(fileName), aliasPairMap(m), idAssigner(ida), callGraph(cg),currentCS(nullptr){}

  void visitAllocRecord(const AllocRecord &allocRecord);
  void visitPointerRecord(const PointerRecord &);
  void visitEnterRecord(const EnterRecord &);
  void visitExitRecord(const ExitRecord &);
  void visitCallRecord(const CallRecord &);
};

/// Checks if two point-to sets have any common addresses
bool AnalysisImpl::intersects(const PtsSet &lhs, const PtsSet &rhs) {
  for (const auto *ptr : lhs) {
    if (rhs.count(ptr))
      return true;
  }
  return false;
}

/// Analyzes the current function frame to find all alias pairs.
/// Compares local pointers with each other and with globals.
void AnalysisImpl::findAliasPairs() {
  auto func = stackFrames.back().func;
  auto &summary = aliasPairMap[func];
  auto const &localMap = stackFrames.back().localMap;

  for (auto itr = localMap.begin(), ite = localMap.end(); itr != ite; ++itr) {
    auto itr2 = itr;
    for (++itr2; itr2 != ite; ++itr2) {
      if (intersects(itr->second, itr2->second))
        summary.insert(AliasPair(itr->first, itr2->first));
    }
  }

  for (auto itr = localMap.begin(), ite = localMap.end(); itr != ite; ++itr) {
    for (auto itr2 = globalMap.begin(), ite2 = globalMap.end(); itr2 != ite2;
         ++itr2) {
      if (itr->second.count(itr2->second))
        summary.insert(AliasPair(itr->first, itr2->first));
    }
  }
}

/// Records a memory allocation, tracking the address for the pointer ID
void AnalysisImpl::visitAllocRecord(const AllocRecord &allocRecord) {
  if (allocRecord.type == AllocType::Global) {
    if (!isa<llvm::GlobalValue>(idAssigner.getValue(allocRecord.id))) { exit(-1); }
    globalMap[cast<llvm::GlobalValue>(idAssigner.getValue(allocRecord.id))] = allocRecord.address;
  } else {
    stackFrames.back().localMap[
      cast<llvm::Instruction>(idAssigner.getValue(allocRecord.id))].insert(allocRecord.address);
  }
}

/// Records a pointer assignment, adding the target address to the pointer's
/// point-to set
void AnalysisImpl::visitPointerRecord(const PointerRecord &ptrRecord) {
  const auto *ptr = idAssigner.getValue(ptrRecord.id);
  if (isa<Instruction>(ptr)) {
    stackFrames.back().localMap[cast<Instruction>(ptr)].insert(ptrRecord.address);
  } else if (isa<Argument>(ptr)) {
    stackFrames.back().localMap[cast<Argument>(ptr)].insert(ptrRecord.address);
  }
}

/// Pushes a new function frame onto the call stack
void AnalysisImpl::visitEnterRecord(const EnterRecord &enterRecord) {
  stackFrames.push_back(Frame{cast<llvm::Function>(idAssigner.getValue(enterRecord.id)), LocalMap()});
  if (currentCS){
    // outs()<<"[debug] callee hex:" << reinterpret_cast<size_t>(idAssigner.getValue(enterRecord.id)) << "\n";
    // if (!idAssigner.getValue(enterRecord.id)) outs()<<"[error] nullptr\n\n";
    outs()<<currentCS->getFunction()->getName()<<","<<
      /*callsite omitted*/","<<
      cast<const llvm::Function>(idAssigner.getValue(enterRecord.id))->getName()<<"\n";
    callGraph.addResolvedCallEdge(currentCS,
      currentCS->getFunction(),
      cast<const llvm::Function>(idAssigner.getValue(enterRecord.id))
    );
  }
}

/// Pops the current function frame and analyzes aliases before exiting
void AnalysisImpl::visitExitRecord(const ExitRecord &exitRecord) {
  if (stackFrames.back().func != idAssigner.getValue(exitRecord.id))
    throw std::logic_error("Function entry/exit do not match");
  findAliasPairs();
  stackFrames.pop_back();
}

/// Records a function call (currently unused)
void AnalysisImpl::visitCallRecord(const CallRecord &callRecord) {
  currentCS=cast<const llvm::Instruction>(idAssigner.getValue(callRecord.id));
}
} // namespace

DynamicAliasAnalysis::DynamicAliasAnalysis(llvm::Module &M, const char* fileName)
    : aliasPairMap(), fileName(fileName), M(M), idAssigner(M), callGraph(M) {}

/// Processes the log file and populates the alias pair map
void DynamicAliasAnalysis::runAnalysis() {
  AnalysisImpl(fileName, aliasPairMap, idAssigner, callGraph).process();
}

/// Returns alias pairs for a given function pointer, or nullptr if none found
const DynamicAliasAnalysis::AliasPairSet *
DynamicAliasAnalysis::getAliasPairs(const llvm::Function *p) const {
  auto itr = aliasPairMap.find(p);
  if (itr == aliasPairMap.end())
    return nullptr;
  return &itr->second;
}
} // namespace dynamic