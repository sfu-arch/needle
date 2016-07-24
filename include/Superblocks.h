#ifndef SUPERBLOCKS_H
#define SUPERBLOCKS_H

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <map>

namespace sb {

// R=Real F=Fake I=In O=Out
enum PathType { RIRO, FIRO, RIFO, FIFO, SELF };

struct Path {
    std::string Id;
    uint64_t Freq;
    PathType PType;
    std::vector<std::string> Seq;
    std::set<llvm::Value *> LiveIn, LiveOut, MemIn, MemOut, Globals;
};

typedef std::pair<llvm::BasicBlock *, llvm::BasicBlock *> Edge;

std::function<bool(const Edge &, const Edge &)> getCmp();
std::vector<llvm::Loop *> getInnermostLoops(llvm::LoopInfo &LI);

struct Superblocks : public llvm::ModulePass {
    static char ID;
    std::string SeqFilePath;
    int NumSeq;
    std::vector<Path> Sequences;
    std::function<bool(const Edge &, const Edge &)> KeyCmp;
    std::map<sb::Edge, llvm::APInt, decltype(KeyCmp)> EdgeProfile;
    std::map<std::string, uint32_t> Data;

    Superblocks(std::string S)
        : llvm::ModulePass(ID), SeqFilePath(S), EdgeProfile(getCmp()) {}

    virtual bool runOnModule(llvm::Module &M) override;
    virtual bool doInitialization(llvm::Module &M) override;
    virtual bool doFinalization(llvm::Module &M) override;

    void makeEdgeProfile(std::map<std::string, llvm::BasicBlock *> &);
    void readSequences();
    void process(llvm::Function &F);
    void hyperblock(llvm::Loop*, llvm::LoopInfo&);
    void
    construct(llvm::BasicBlock *Begin,
              llvm::SmallVector<llvm::SmallVector<llvm::BasicBlock *, 8>, 32>
                  &Superblocks,
              llvm::DenseSet<std::pair<const llvm::BasicBlock *,
                                       const llvm::BasicBlock *>> &BackEdges);

    virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const override {
        AU.addRequired<llvm::LoopInfoWrapperPass>();
		AU.addRequired<llvm::DominatorTreeWrapperPass>();
        AU.setPreservesAll();
    }
};
}

#endif
