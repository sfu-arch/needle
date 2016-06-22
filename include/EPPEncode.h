#ifndef EPPENCODE_H
#define EPPENCODE_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/UnifyFunctionExitNodes.h"

#include <map>
#include <unordered_map>

#include "AltCFG.h"

namespace epp {

enum EdgeType {
    EHEAD,
    ELATCH,
    ELIN,
    ELOUT1, // To Exit
    ELOUT2, // From Entry
    EREAL,
    EOUT
};

struct Edge {
    llvm::BasicBlock *Src, *Tgt;
    std::shared_ptr<Edge> Fakes[2]; 
    EdgeType Type;
    //uint64_t Id;
    Edge(llvm::BasicBlock *S, llvm::BasicBlock *T, EdgeType ET)
        : Src(S), Tgt(T), Type(ET) {}
    llvm::BasicBlock *src() { return Src; }
    llvm::BasicBlock *tgt() { return Tgt; }
    static std::shared_ptr<Edge> makeEdge(llvm::BasicBlock *S,
                                          llvm::BasicBlock *T, EdgeType ET) {
        return std::make_shared<Edge>(S, T, ET);
    }
    bool operator==(const Edge &E) {
        if (E.Src == Src && E.Tgt == Tgt && E.Type == Type)
            return true;
        return false;
    }
};

struct EPPEncode : public llvm::FunctionPass {

    static char ID;

    llvm::LoopInfo *LI;
    llvm::DenseMap<llvm::BasicBlock *, llvm::APInt> numPaths;
    std::unordered_map<std::shared_ptr<Edge>, llvm::APInt> Val;
    std::unordered_map<std::shared_ptr<Edge>, llvm::APInt> Inc;
    altepp::altcfg test;

    EPPEncode() : llvm::FunctionPass(ID), LI(nullptr) {}

    virtual void getAnalysisUsage(llvm::AnalysisUsage &au) const override {
        au.addRequired<llvm::LoopInfoWrapperPass>();
        au.setPreservesAll();
    }

    virtual bool runOnFunction(llvm::Function &f) override;
    void encode(llvm::Function &f);
    //bool isTargetFunction(llvm::Function &f,
                          //llvm::cl::list<std::string> &FunctionList) const;
    bool doInitialization(llvm::Module &m) override;
    bool doFinalization(llvm::Module &m) override;
    virtual void releaseMemory() override;
    const char *getPassName() const override { return "PASHA - EPPEncode"; }
};

typedef std::pair<llvm::BasicBlock *, EdgeType> BlockEdgeTy;

struct BlockEdgeTyKeyInfo {
    static inline BlockEdgeTy getEmptyKey() {
        return std::make_pair(nullptr, EHEAD);
    }
    static inline BlockEdgeTy getTombstoneKey() {
        return std::make_pair(nullptr, EOUT);
    }
    static unsigned getHashValue(const BlockEdgeTy &Key) {
        return static_cast<unsigned>(hash_value(Key));
    }
    static bool isEqual(const BlockEdgeTy &LHS, const BlockEdgeTy &RHS) {
        return LHS.first == RHS.first && LHS.second == RHS.second;
    }
};


}
#endif
