#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Pass.h"
#include "Common.h"

#include "EPPEncode.h"

#include <algorithm>
#include <vector>
#include <set>
#include <unordered_set>
#include <stack>
#include <cassert>

using namespace llvm;
using namespace epp;
using namespace std;

#define DEBUG_TYPE "epp_encode"

bool EPPEncode::doInitialization(Module &m) { return false; }

bool EPPEncode::doFinalization(Module &m) { return false; }

bool EPPEncode::runOnFunction(Function &func) {
    LI = &getAnalysis<LoopInfo>();
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

enum BlockType { LOOPHEAD, LOOPBODY, LOOPLATCH, LOOPEXIT, UNLOOP };

static BlockType getBlockType(BasicBlock *BB, LoopInfo *LI) {
    Loop *L = LI->getLoopFor(BB);
    if (!L)
        return UNLOOP;

    if (L->getHeader() == BB)
        return LOOPHEAD;

    if (L->getLoopLatch() == BB)
        return LOOPLATCH;

    if (L->isLoopExiting(BB))
        return LOOPEXIT;

    return LOOPBODY;
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

    // TODO: Check for functions that don't return
    // TODO: longjump ?

    return false;
}

static shared_ptr<Edge>
findEdgeInVal(const BasicBlock *Src, const BasicBlock *Tgt,
              const unordered_map<shared_ptr<Edge>, llvm::APInt> &Val) {
    for (auto &V : Val)
        if (V.first->src() == Src && V.first->tgt() == Tgt &&
            V.first->Type == EREAL)
            return V.first;

    assert(false && "This should be unreachable");
    return nullptr;
}

static void
functionDFSTreeHelper(BasicBlock *toVisit, set<shared_ptr<Edge>> &ST,
                      DenseSet<BasicBlock *> &Seen,
                      const unordered_map<shared_ptr<Edge>, llvm::APInt> &Val) {
    Seen.insert(toVisit);
    for (auto S = succ_begin(toVisit), E = succ_end(toVisit); S != E; ++S) {
        if (!Seen.count(*S)) {
            ST.insert(findEdgeInVal(toVisit, *S, Val));
            functionDFSTreeHelper(*S, ST, Seen, Val);
        }
    }
}

static set<shared_ptr<Edge>>
getSpanningTree(Function &F,
                const unordered_map<shared_ptr<Edge>, llvm::APInt> &Val) {
    set<shared_ptr<Edge>> SpanningTree;
    DenseSet<BasicBlock *> Seen;

    // Find a spanning tree using DFS of DAG
    // representation of function.
    functionDFSTreeHelper(&F.getEntryBlock(), SpanningTree, Seen, Val);

    return SpanningTree;
}

static set<shared_ptr<Edge>>
getChords(const unordered_map<shared_ptr<Edge>, APInt> &Edges,
          const set<shared_ptr<Edge>> &ST) {
    set<shared_ptr<Edge>> Chords;

    for (auto &E : Edges)
        if (ST.count(E.first) == 0)
            Chords.insert(E.first);
    return Chords;
}

static APInt dir(shared_ptr<Edge> E, shared_ptr<Edge> F) {
    if (E == nullptr)
        return APInt(256, StringRef("1"), 10);
    // return 1;
    else if (E->src() == F->tgt() || E->tgt() == F->src())
        return APInt(256, StringRef("1"), 10);
    // return 1;
    else
        return APInt(256, StringRef("-1"), 10);
    // return -1;
}

static void incDFSHelper(APInt Events, BasicBlock *V, shared_ptr<Edge> E,
                         const set<shared_ptr<Edge>> &ST,
                         const set<shared_ptr<Edge>> &Chords,
                         unordered_map<shared_ptr<Edge>, APInt> &Val,
                         unordered_map<shared_ptr<Edge>, APInt> &Inc) {
    //  for each f belongs to T : f != e and v = tgt( f ) do
    //      DFS( Dir(e, f ) * events + Events( f ) , src( f ) , f )
    //  od
    //  for each f belongs to T : f != e and v = src( f ) do
    //      DFS( Dir(e, f ) * events + Events( f ) , tgt( f ) , f )
    //  od
    //  for each f belongs to E − T : v = src( f ) or v = tgt( f ) do
    //      Increment( f ) : = Increment( f ) + Dir(e, f ) * events
    //  fi

    for (auto &F : ST) {
        if (E != F && V == F->tgt())
            incDFSHelper(dir(E, F) * Events + Val[F], F->src(), F, ST, Chords,
                         Val, Inc);
    }

    for (auto &F : ST) {
        if (E != F && V == F->src())
            incDFSHelper(dir(E, F) * Events + Val[F], F->tgt(), F, ST, Chords,
                         Val, Inc);
    }

    for (auto &C : Chords) {
        if (V == C->src() || V == C->tgt())
            Inc[C] = Inc[C] + dir(E, C) * Events;
    }
}

static void computeIncrement(BasicBlock *Entry, BasicBlock *LastTopoExit,
                             unordered_map<shared_ptr<Edge>, APInt> &Inc,
                             unordered_map<shared_ptr<Edge>, APInt> &Val,
                             const set<shared_ptr<Edge>> &ST) {
    auto Chords = getChords(Val, ST);

    // Add EXIT -> ENTRY chord, Sec 3.3
    // required for correct computation.
    auto EE = Edge::makeEdge(LastTopoExit, Entry, EOUT);
    Chords.insert(EE);

    DEBUG(errs() << "\nChords :\n");
    for (auto &C : Chords)
        DEBUG(errs() << C->src()->getName() << " -> " << C->tgt()->getName()
                     << "\n");

    // Implements Efficient Event Counting algorithm
    // Figure 4 in the paper.

    for (auto &C : Chords)
        Inc[C] = APInt(256, StringRef("0"), 10);

    incDFSHelper(APInt(256, StringRef("0"), 10), Entry, nullptr, ST, Chords,
                 Val, Inc);

    // The EXIT->ENTRY edge does not exist in Val,
    // however using Val[C] where C = EXIT->ENTRY
    // creates a new map entry with default value
    // of uint64_t (0) and so it's all good.

    Val.insert(make_pair(EE, APInt(256, StringRef("0"), 10)));

    for (auto &C : Chords) {
        Inc[C] = Inc[C] + Val[C];
    }
}

void EPPEncode::releaseMemory() {
    LI = nullptr;
    numPaths.clear();
    Val.clear();
    Inc.clear();
    selfLoopCounter = 0;
}


void EPPEncode::encode(Function &F) {
    DEBUG(errs() << "Called Encode on " << F.getName() << "\n");

    SetVector<BasicBlock *> BackedgeTargets;
    BasicBlock *LastTopoExit = nullptr;

    for (auto &POB : functionPostorderTraversal(F, LI)) {
        APInt pathCount(256, StringRef("0"), 10);

        // Save the function topo exit block
        // to be used later
        if (!LastTopoExit)
            LastTopoExit = POB;

        // Leaves
        if (isFunctionExiting(POB))
            pathCount = 1;

        // Normal Successors
        for (auto S = succ_begin(POB), E = succ_end(POB); S != E; S++) {
            if (*S == POB) {
                selfLoopMap[selfLoopCounter++] = POB;
                continue;
            }

            if (getBlockType(*S, LI) == LOOPHEAD &&
                getBlockType(POB, LI) == LOOPLATCH) {
                BackedgeTargets.insert(*S);
                continue;
            }

            DEBUG(errs() << "Adding Edge: " << POB->getName() << " "
                         << (*S)->getName() << " " << pathCount << "\n");
            Val.insert(make_pair(Edge::makeEdge(POB, *S, EREAL), pathCount));

            if (numPaths.count(*S) == 0)
                numPaths.insert(make_pair(*S, APInt(256, StringRef("0"), 10)));

            pathCount += numPaths[*S];
        }

        // NULL edges from ENTRY->HEADER
        if (POB == &F.getEntryBlock()) {
            // Add all loop headers as successors
            // This is OK as we will visit the function
            // entry last (rev topo order) and all the
            // targets have been saved.
            for (auto &BT : BackedgeTargets) {
                Val.insert(
                    make_pair(Edge::makeEdge(POB, BT, ENULL), pathCount));
                assert(numPaths.count(BT) &&
                       "All the backedge targets should be present");
                pathCount += numPaths[BT];
            }
        }

        // NULL edges from LOOPEXIT->EXIT
        else if (getBlockType(POB, LI) == LOOPLATCH) {
            // Add the function exit block as a successor
            // since this is a backedge source.
            Val.insert(
                make_pair(Edge::makeEdge(POB, LastTopoExit, ENULL), pathCount));
            assert(numPaths.count(LastTopoExit) &&
                   "LastTopoExit should be initialized already");
            pathCount += numPaths[LastTopoExit];
        }

        DEBUG(errs() << "Numpaths " << POB->getName() << " " << pathCount
                     << "\n");
        numPaths.insert(make_pair(POB, pathCount));
    }
    DEBUG(errs() << "\n");

    DEBUG(errs() << "\nEdge Weights :\n");
    for (auto &V : Val)
        DEBUG(errs() << V.first->src()->getName() << " -> "
                     << V.first->tgt()->getName() << " " << V.second << "\n");

    DEBUG(errs() << "\nPath Counts :\n");
    for (auto &P : numPaths)
        DEBUG(errs() << P.first->getName() << " -> " << P.second << "\n");

    auto T = getSpanningTree(F, Val);

    DEBUG(errs() << "\nSpanning Tree :\n");
    for (auto &E : T)
        DEBUG(errs() << E->src()->getName() << " -> " << E->tgt()->getName()
                     << "\n");

    computeIncrement(&F.getEntryBlock(), LastTopoExit, Inc, Val, T);

    DEBUG(errs() << "\nVals :\n");
    for (auto &V : Val)
        DEBUG(errs() << V.first->src()->getName() << " -> "
                     << V.first->tgt()->getName() << " " << V.second << "\n");

    DEBUG(errs() << "\nIncrements :\n");
    for (auto &I : Inc)
        DEBUG(errs() << I.first->src()->getName() << " -> "
                     << I.first->tgt()->getName() << " " << I.second << "\n");

    //DEBUG(errs() << "NumPaths : " << numPaths[&F.getEntryBlock()] << "\n");
    errs() << "NumPaths : " << numPaths[&F.getEntryBlock()] << "\n";
}

char EPPEncode::ID = 0;
static RegisterPass<EPPEncode> X("", "Efficient Path Profiling -- Encoding");
