#ifndef MICROWORKLOAD_H
#define MICROWORKLOAD_H

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"

#include <sstream>
#include <string>
#include <algorithm>
#include <fstream>
#include <set>
#include <map>

namespace mwe {

// R=Real F=Fake I=In O=Out
enum PathType { RIRO, FIRO, RIFO, FIFO, SELF };

struct Path {
    std::string Id;
    uint64_t Freq;
    PathType PType;
    std::vector<std::string> Seq;
    std::set<llvm::Value *> LiveIn, LiveOut, MemIn, MemOut, Globals;
};

struct MicroWorkloadExtract : public llvm::ModulePass {
    static char ID;
    std::string SeqFilePath;
    int NumSeq;
    std::vector<Path> Sequences;
    llvm::PostDominatorTree *PostDomTree;
    llvm::AliasAnalysis *AA;
    bool extractAsChop;

    MicroWorkloadExtract(std::string S, int N, int C)
        : llvm::ModulePass(ID), SeqFilePath(S), NumSeq(N), extractAsChop(C) {}

    virtual bool runOnModule(llvm::Module &M) override;
    virtual bool doInitialization(llvm::Module &M) override;
    virtual bool doFinalization(llvm::Module &M) override;

    void readSequences();

    void process(llvm::Function &F);
    llvm::Function* extract(llvm::PostDominatorTree* , llvm::Module*,
                                   llvm::SmallVector<llvm::BasicBlock *, 16>&);
    
    void 
    extractHelper(llvm::Function *, llvm::Function *,
                      llvm::SmallVector<llvm::Value *, 16> &LiveIn, llvm::SetVector<llvm::Value *>&,
                      llvm::SetVector<llvm::Value *> &,
                      llvm::SmallVector<llvm::BasicBlock *, 16> &,
                      llvm::LLVMContext &); 

    virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const override {
        AU.addRequired<llvm::AliasAnalysis>();
        AU.addRequired<llvm::DominatorTreeWrapperPass>();
        AU.addRequired<llvm::PostDominatorTree>();
        AU.setPreservesAll();
    }
};

struct MicroWorkloadHelper : public llvm::ModulePass {
    static char ID;    
    llvm::Function *Offload, *Undo;

    MicroWorkloadHelper(llvm::Function* F, llvm::Function* U) : llvm::ModulePass(ID) ,
            Offload(F), Undo(U) {}

    virtual bool runOnModule(llvm::Module &M) override;
    virtual bool doInitialization(llvm::Module &M) override;
    virtual bool doFinalization(llvm::Module &M) override;

    void addUndoLog();
    void replaceGuards();

    virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const override {
        AU.addRequired<llvm::AliasAnalysis>();
    }
};

}

#endif
