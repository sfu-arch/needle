#define DEBUG_TYPE "loop_converter"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Dominators.h" 
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>

#include "LoopConverter.h"
#include "Common.h"

using namespace llvm;
using namespace std;
using namespace pasha;

// Convert self loops to normal loops, this makes the 
// segmentation easier and also cleans up other special
// cases.
// Reqs : Run LoopSimplify before
// Reqs : Run LoopInfo after

bool LoopConverter::runOnModule(Module &M) {

    auto updatePhis = [](BasicBlock* Tgt, BasicBlock *New) {
        for(auto &I : *Tgt) {
            if(auto *Phi = dyn_cast<PHINode>(&I)) {
                Phi->setIncomingBlock(Phi->getBasicBlockIndex(Tgt), New);
            }
        }
    };

    auto insertLatch = [&updatePhis](BasicBlock* BB) {
        auto *Latch = BasicBlock::Create(BB->getContext(), 
                       BB->getName() + ".latch", BB->getParent());
        auto *T = BB->getTerminator();
        T->replaceUsesOfWith(BB, Latch);
        updatePhis(BB, Latch);
        BranchInst::Create(BB, Latch);
    };

    for(auto &F : M) {
        for(auto &BB : F) {
            if(common::isSelfLoop(&BB)) {
                insertLatch(&BB);
            }
        }
    }
    return true;
}

char LoopConverter::ID = 0;
static RegisterPass<LoopConverter> X("", "PASHA - LoopConverter");
