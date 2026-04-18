#pragma once

#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/IR/Value.h>

using namespace llvm;

class AliasRecord {
private:
  const Value *first;
  const Value *second;
  AliasResult res;

public:
  AliasRecord(const Value *p0, const Value *p1, AliasResult r) : res(r) {
    assert(r != AliasResult::NoAlias && "NoAlias should not be stored");
    if (p0 < p1) {
      first = p0;
      second = p1;
    } else {
      first = p1;
      second = p0;
    }
  }

  const Value *getFirst() const { return first; }
  const Value *getSecond() const { return second; }
  AliasResult getResult() const { return res; }
};

inline bool operator==(const AliasRecord &lhs, const AliasRecord &rhs) {
  return lhs.getFirst() == rhs.getFirst() && lhs.getSecond() == rhs.getSecond();
}

inline bool operator!=(const AliasRecord &lhs, const AliasRecord &rhs) {
  return !(lhs == rhs);
}

inline bool operator<(const AliasRecord &lhs, const AliasRecord &rhs) {
  return (lhs.getFirst() < rhs.getFirst()) ||
         (lhs.getFirst() == rhs.getFirst() && lhs.getSecond() < rhs.getSecond());
}

inline bool operator>=(const AliasRecord &lhs, const AliasRecord &rhs) {
  return !(lhs < rhs);
}

inline bool operator>(const AliasRecord &lhs, const AliasRecord &rhs) {
  return rhs < lhs;
}

inline bool operator<=(const AliasRecord &lhs, const AliasRecord &rhs) {
  return !(rhs < lhs);
}