#define DEBUG_TYPE "epp_encode"
#include "Common.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Support/FileSystem.h"

#include "EPPEncode.h"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <set>
#include <stack>
#include <unordered_set>
#include <vector>

using namespace llvm;
using namespace epp;
using namespace std;



bool EPPEncode::doInitialization(Module &m) { return false; }
bool EPPEncode::doFinalization(Module &m) { return false; }

bool EPPEncode::runOnFunction(Function &func) {
    LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    encode(func);
    return false;
}

static void loopPostorderHelper(BasicBlock *toVisit, const Loop *loop,
                                vector<BasicBlock *> &blocks,
                                DenseSet<BasicBlock *> &seen,
                                const set<BasicBlock *> &SCCBlocks) {
    seen.insert(toVisit);
    for (auto s = succ_begin(toVisit), e = succ_end(toVisit); s != e; ++s) {
        // Don't need to worry about backedge successors as their targets
        // will be visited already and will fail the first condition check.

        if (!seen.count(*s) && (SCCBlocks.find(*s) != SCCBlocks.end())) {
            loopPostorderHelper(*s, loop, blocks, seen, SCCBlocks);
        }
    }
    blocks.push_back(toVisit);
}

static vector<BasicBlock *>
loopPostorderTraversal(const Loop *loop, const set<BasicBlock *> &SCCBlocks) {
    vector<BasicBlock *> ordered;
    DenseSet<BasicBlock *> seen;
    loopPostorderHelper(loop->getHeader(), loop, ordered, seen, SCCBlocks);
    return ordered;
}

// LoopInfo contains a mapping from basic block to the innermost loop. Find
// the outermost loop in the loop nest that contains BB.
static const Loop *getOutermostLoop(const LoopInfo *LI, const BasicBlock *BB) {
    const Loop *L = LI->getLoopFor(BB);
    if (L) {
        while (const Loop *Parent = L->getParentLoop())
            L = Parent;
    }
    return L;
}

static const vector<BasicBlock *> functionPostorderTraversal(Function &F,
                                                             LoopInfo *LI) {
    vector<BasicBlock *> PostOrderBlocks;
    // outs() << "SCCs for " << F.getName() << " in post-order:\n";
    for (auto I = scc_begin(&F), IE = scc_end(&F); I != IE; ++I) {
        // Obtain the vector of BBs
        const std::vector<BasicBlock *> &SCCBBs = *I;

        // Any SCC with more than 1 BB is a loop, however if there is a self
        // referential
        // basic block then that will be counted as a loop as well.
        if (I.hasLoop()) {
            // Since the SCC is a fully connected components,
            // for a loop nest using *any* BB should be sufficient
            // to get the outermost loop.

            auto *OuterLoop = getOutermostLoop(LI, SCCBBs[0]);

            // Get the blocks as a set to perform fast test for SCC membership
            set<BasicBlock *> SCCBlocksSet(SCCBBs.begin(), SCCBBs.end());

            // Get the loopy blocks in post order
            auto blocks = loopPostorderTraversal(OuterLoop, SCCBlocksSet);

            assert(SCCBBs.size() == blocks.size() &&
                   "Could not discover all blocks");

            for (auto *BB : blocks) {
                PostOrderBlocks.emplace_back(BB);
            }
        } else {
            // There is only 1 BB in this vector
            auto BBI = SCCBBs.begin();
            PostOrderBlocks.emplace_back(*BBI);
        }
    }

    DEBUG(errs() << "Post Order Blocks: \n");
    for (auto &POB : PostOrderBlocks)
        DEBUG(errs() << POB->getName() << " ");
    DEBUG(errs() << "\n");

    return PostOrderBlocks;
}


static bool isFunctionExiting(BasicBlock *BB) {
    if (BB->getTerminator()->getNumSuccessors() == 0)
        return true;

    return false;
}

void EPPEncode::releaseMemory() {
    LI = nullptr;
    numPaths.clear();
    ACFG.clear();
}

void EPPEncode::encode(Function &F) {
    DEBUG(errs() << "Called Encode on " << F.getName() << "\n");

    auto POB = functionPostorderTraversal(F, LI);
    auto Entry = POB.back(), Exit = POB.front();
    auto BackEdges = common::getBackEdges(F);

    // Add real edges
    for (auto &BB : POB) {
        for (auto S = succ_begin(BB), E = succ_end(BB); S != E; S++) {
            if (BackEdges.count(make_pair(BB, *S)) ||
                LI->getLoopFor(BB) != LI->getLoopFor(*S)) {
                DEBUG(errs() << "Adding segmented edge : " << BB->getName() << " "
                    << S->getName() << " " << Entry->getName() << " " << Exit->getName() << "\n");
                ACFG.add(BB, *S, Entry, Exit);
                continue;
            }
            DEBUG(errs() << "Adding Real edge : " << BB->getName() << " " << S->getName() << "\n");
            ACFG.add(BB, *S);
        }
    }


    for(auto &B : POB) {
        APInt pathCount(128, 0, true);

        if (isFunctionExiting(B)) 
            pathCount = 1;

        for(auto &S : ACFG.succs(B)) {
            ACFG[{B,S}] = pathCount;
            if (numPaths.count(S) == 0)
                numPaths.insert(make_pair(S, APInt(128, 0, true)));
             
            // This is the only place we need to check for overflow.
            bool Ov = false;
            pathCount = pathCount.sadd_ov(numPaths[S], Ov);
            assert(!Ov && "Integer Overflow");
        }
        numPaths.insert({B, pathCount});
    }
   
    errs() << "NumPaths : " << numPaths[Entry] << "\n";
}

char EPPEncode::ID = 0;
static RegisterPass<EPPEncode> X("", "PASHA - EPPEncode");
