#ifndef MICROWORKLOAD_H
#define MICROWORKLOAD_H

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

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

namespace mwe {

// R=Real F=Fake I=In O=Out
enum PathType { RIRO, FIRO, RIFO, FIFO };
enum ExtractType { trace, slice, merge };

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
    std::vector<Path> Sequences;
    llvm::PostDominatorTree *PostDomTree;
    llvm::AliasAnalysis *AA;
    bool extractAsChop;
    std::vector<std::unique_ptr<llvm::Module>> &ExtractedModules;

    std::map<std::string, uint64_t> Data;

    MicroWorkloadExtract(std::string S, 
                     std::vector<std::unique_ptr<llvm::Module>> &EM);

    virtual bool runOnModule(llvm::Module &M) override;
    virtual bool doInitialization(llvm::Module &M) override;
    virtual bool doFinalization(llvm::Module &M) override;

    void readSequences();

    void process(llvm::Function &F);
    llvm::Function *extract(llvm::PostDominatorTree *, llvm::Module *,
                            llvm::SmallVector<llvm::BasicBlock *, 16> &,
                            llvm::SetVector<llvm::Value *> &,
                            llvm::SetVector<llvm::Value *> &,
                            llvm::SetVector<llvm::Value *> &,
                            llvm::DominatorTree *, llvm::LoopInfo *,
                            std::string);

    void extractHelper(llvm::Function *, llvm::Function *,
                       llvm::SetVector<llvm::Value *> &LiveIn,
                       llvm::SetVector<llvm::Value *> &,
                       llvm::SetVector<llvm::Value *> &,
                       llvm::SmallVector<llvm::BasicBlock *, 16> &,
                       llvm::LLVMContext &);

    virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const override {
        AU.addRequired<llvm::AAResultsWrapperPass>();
        AU.addRequired<llvm::DominatorTreeWrapperPass>();
        AU.addRequired<llvm::LoopInfoWrapperPass>();
        AU.addRequired<llvm::PostDominatorTree>();
    }
};

struct MicroWorkloadHelper : public llvm::ModulePass{
    static char ID;
    std::string Id;
    std::map<std::string, uint64_t> Data;
    MicroWorkloadHelper(std::string I)
        : llvm::ModulePass(ID), Id(I) {}


    virtual bool runOnModule(llvm::Module&) override;
    virtual bool doInitialization(llvm::Module &M) override;
    virtual bool doFinalization(llvm::Module &M) override;

    void addUndoLog(llvm::Function *);
    void replaceGuards(llvm::Function *);

    virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const override {
        AU.addRequired<llvm::AAResultsWrapperPass>();
    }
};
}

#endif
