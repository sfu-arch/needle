#ifndef NAMER_H
#define NAMER_H

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/ScalarEvolution.h"

using namespace llvm;

namespace pasha {

struct Statistics : public FunctionPass {
    static char ID;

    std::map<std::string, uint64_t> OpcodeCount;
    std::map<std::string, uint64_t> OpcodeWt;

    std::map<std::string, uint64_t> Data;

    Statistics() : FunctionPass(ID) {}

    virtual bool doInitialization(Module &M) override;
    virtual bool doFinalization(Module &M) override;
    virtual bool runOnFunction(Function &F) override;

    void generalStats(Function&);
    std::vector<std::pair<llvm::BasicBlock*, uint64_t>> 
                    criticalPath(Function&);
    uint64_t getBlockSize(BasicBlock *);
    void memoryToBranchDependency(Function&);
    void branchToMemoryDependency(Function&);

    void getAnalysisUsage(AnalysisUsage &AU) const override {}
};

struct BranchTaxonomy : public ModulePass {
    static char ID;
    std::string FName;
    std::map<std::string, uint64_t> Data;

    BranchTaxonomy(std::string N) : FName(N), ModulePass(ID) {}

    virtual bool doInitialization(Module &M) override;
    virtual bool doFinalization(Module &M) override;
    virtual bool runOnModule(Module &M) override;

    void loopBounds(Function&);
    void functionDAG(Function&);

    void getAnalysisUsage(AnalysisUsage &AU) const override {
        AU.addRequired<LoopInfoWrapperPass>();
        AU.addRequired<ScalarEvolutionWrapperPass>();
    }
};

}

#endif
