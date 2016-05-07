#ifndef EPPRT_H
#define EPPRT_H

// This header is for debugging only, and not required when the
// actual instrumentation is done since the prototupes are injected
// into the bitcode automatically.

#include <cstdint>
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/Hashing.h"

extern "C" {

#define EPP(X) PaThPrOfIlInG_##X

namespace llvm {

struct DenseMapAPIntKeyInfo {
    static inline llvm::APInt getEmptyKey() {
        llvm::APInt V(nullptr, 0);
        V.VAL = 0;
        return V;
    }
    static inline llvm::APInt getTombstoneKey() {
        llvm::APInt V(nullptr, 0);
        V.VAL = 1;
        return V;
    }
    static unsigned getHashValue(const llvm::APInt &Key) {
        return static_cast<unsigned>(hash_value(Key));
    }
    static bool isEqual(const llvm::APInt &LHS, const llvm::APInt &RHS) {
        return LHS.getBitWidth() == RHS.getBitWidth() && LHS == RHS;
    }
};
}

void EPP(incCount)(uint64_t qw0, uint64_t qw1);

void EPP(logPath)();

void EPP(save)();
}
#endif
