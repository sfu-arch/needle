#define DEBUG_TYPE "epp_encode"
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
#include "llvm/ADT/MapVector.h"

#include "EPPEncode.h"

#include <algorithm>
#include <vector>
#include <set>
#include <unordered_set>
#include <stack>
#include <cassert>
#include <fstream>

using namespace llvm;
using namespace epp;
using namespace std;


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

//enum BlockType { LOOPHEAD, LOOPBODY, LOOPLATCH, LOOPEXIT, UNLOOP };

//static BlockType getBlockType(BasicBlock *BB, LoopInfo *LI) {
    //Loop *L = LI->getLoopFor(BB);
    //if (!L)
        //return UNLOOP;

    //if (L->getHeader() == BB)
        //return LOOPHEAD;

    //if (L->getLoopLatch() == BB)
        //return LOOPLATCH;

    //if (L->isLoopExiting(BB))
        //return LOOPEXIT;

    //return LOOPBODY;
//}

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
findEdgeInVal(const BasicBlock *Src, const BasicBlock *Tgt, const EdgeType Ty,
              const unordered_map<shared_ptr<Edge>, llvm::APInt> &Val) {
    for (auto &V : Val)
        if (V.first->src() == Src && V.first->tgt() == Tgt &&
            V.first->Type == Ty)
            return V.first;

    assert(false && "This should be unreachable");
    return nullptr;
}

static void
spanningHelper(BasicBlock *toVisit, set<shared_ptr<Edge>> &ST,
                      DenseSet<BasicBlock *> &Seen,
                      MapVector<BasicBlock *, SmallVector<pair<BasicBlock*, EdgeType>, 4>> &AltCFG,
                      const unordered_map<shared_ptr<Edge>, llvm::APInt> &Val) {
    Seen.insert(toVisit);
    //for (auto S = succ_begin(toVisit), E = succ_end(toVisit); S != E; ++S) {
    auto &SV = AltCFG[toVisit];
    for(auto &KV : SV){
        auto *S = KV.first;
        auto ET = KV.second;
        if (!Seen.count(S)) {
            ST.insert(findEdgeInVal(toVisit, S, ET, Val));
            spanningHelper(S, ST, Seen, AltCFG, Val);
        }
    }
}

static set<shared_ptr<Edge>>
getSpanningTree(MapVector<BasicBlock *, SmallVector<pair<BasicBlock*, EdgeType>, 4>> &AltCFG,
                      const unordered_map<shared_ptr<Edge>, llvm::APInt> &Val) {
    set<shared_ptr<Edge>> SpanningTree;
    DenseSet<BasicBlock *> Seen;
   
    auto *Entry = AltCFG.back().first;
    spanningHelper(Entry, SpanningTree, Seen, AltCFG, Val);
    
    return SpanningTree;
}

static set<shared_ptr<Edge>>
getChords(const unordered_map<shared_ptr<Edge>, APInt> &Val,
          const set<shared_ptr<Edge>> &ST) {
    set<shared_ptr<Edge>> Chords;
    
    for(auto &V : Val) {
        bool found = false;
        for(auto &SE : ST) {
            if(*V.first == *SE) {
                found = true;
            }
        }
        if(!found) Chords.insert(V.first);
    }

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

static void computeIncrement(BasicBlock *Entry, BasicBlock *Exit,
                             unordered_map<shared_ptr<Edge>, APInt> &Inc,
                             unordered_map<shared_ptr<Edge>, APInt> &Val,
                             const set<shared_ptr<Edge>> &ST,
                             set<shared_ptr<Edge>> &Chords) {

    // Add EXIT -> ENTRY chord, Sec 3.3
    // required for correct computation.
    auto EE = Edge::makeEdge(Exit, Entry, EOUT);
    Chords.insert(EE);

    // Implements Efficient Event Counting algorithm
    // Figure 4 in the paper.

    for (auto &C : Chords)
        Inc[C] = APInt(256, StringRef("0"), 10);

    incDFSHelper(APInt(256, StringRef("0"), 10), Entry, nullptr, ST, Chords,
                 Val, Inc);

    // The EXIT->ENTRY edge does not exist in Val,
    // however using Val[C] where C = EXIT->ENTRY
    // creates a new map entry with default value,
    // instead create explicitly here.

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

    common::printCFG(F);

    //SetVector<BasicBlock *> BackedgeTargets;
    //BasicBlock *LastTopoExit = nullptr;
    auto POB = functionPostorderTraversal(F, LI);
    auto Entry = POB.back(), Exit = POB.front();
    auto BackEdges = common::getBackEdges(F);

    // Alternate representation of CFG which has the fake edges
    // instead of real edges in the following cases:
    // a) loop backedges replaced as described in the paper
    // b) loop entry edges removed
    // c) loop exit edges replaced with edges from loop entry to
    //    exit target block.
    
    MapVector<BasicBlock *, SmallVector<pair<BasicBlock*, EdgeType>, 4>> AltCFG;                        

    // Add real edges
    for(auto &BB : POB) {
        AltCFG.insert(make_pair(BB, SmallVector<pair<BasicBlock*, EdgeType>, 4>()));
        for(auto S = succ_begin(BB), E = succ_end(BB); S != E; S++) {
            if(BackEdges.count(make_pair(BB, *S)) || 
                LI->getLoopFor(BB) != LI->getLoopFor(*S)) continue;
            AltCFG[BB].push_back(make_pair(*S, EREAL));      
        }   
    }   


    SmallVector<Loop* , 16> Loops;
    for(auto &L : *LI) {
        Loops.push_back(L);
        assert(L->getLoopDepth() == 1 && 
                "Expect only top level loops here");
        for(auto &SL : L->getSubLoops()) {
           Loops.push_back(SL); 
        }
    }
      
    // Add all fake edges for loops

    typedef pair<BasicBlock *, BasicBlock *> Key;
    auto PairCmp = [](const Key &A, const Key &B) -> bool {
        return A.first < B.first || (A.first == B.first && A.second < B.second);
    };
    set<Key, decltype(PairCmp)> ExitEdges(PairCmp); 

    for(auto &L : Loops) {
        // 1. Add edge from entry to header
        // 2. Add edge from latch to exit
        // 3. Add edge(s) from header to exit block(s) 
        auto Header = L->getHeader(), 
             PreHeader = L->getLoopPreheader(),
             Latch = L->getLoopLatch();
        
        DEBUG(errs() << "Loop : " << Header->getName() << "\n");
        DEBUG(errs() << "Latch : " << Latch->getName() << "\n");
        assert(Latch && PreHeader && "Run LoopSimplify");
        assert(Header !=  Latch && "Run LoopConverter");
        
        AltCFG[Entry].push_back(make_pair(Header, EHEAD));
        AltCFG[Latch].push_back(make_pair(Exit, ELATCH));
        AltCFG[PreHeader].push_back(make_pair(Exit, ELIN));

        SmallVector<pair<const BasicBlock*, 
            const BasicBlock*>, 4> LoopExitEdges;
        L->getExitEdges(LoopExitEdges);
        for(auto &E : LoopExitEdges) {
            auto *Src = const_cast<BasicBlock*>(E.first);
            auto *Tgt = const_cast<BasicBlock*>(E.second);
            // Exit edges can be the same for two or more 
            // (nested) loops.
            ExitEdges.insert(make_pair(Src, Tgt));
        }
    }

    for(auto &S : ExitEdges) {
        auto *Src = S.first, *Tgt = S.second;
        AltCFG[Src].push_back(make_pair(Exit, ELOUT1));
        AltCFG[Entry].push_back(make_pair(Tgt, ELOUT2));
    }

    DEBUG(errs() << "AltCFG\n");
    for(auto &KV : AltCFG) {
        DEBUG(errs() << KV.first->getName() << " -> ");
        for(auto &S : KV.second) {
            DEBUG(errs() << S.first->getName() << " ");
        }
        DEBUG(errs() << "\n");
    }

    // Dot Printer for AltCFG
    const char *EdgeTypeStr[] = {"EHEAD", "ELATCH", "ELIN", "ELOUT1", "ELOUT2", "EREAL", "EOUT"};
    ofstream DotFile("altcfg.dot", ios::out);
    DotFile << "digraph \"AltCFG\" {\n label=\"AltCFG\";\n";
    for(auto &KV : AltCFG) {
        DotFile << "\tNode" << KV.first << " [shape=record, label=\"" 
            << KV.first->getName().str() << "\"];\n";
        for(auto &S : KV.second) {
            DotFile << "\tNode" << KV.first << " -> Node" << S.first << "[style=";
            if(S.second == EREAL)
                DotFile << "solid,";
            else
                DotFile << "dotted,";
            DotFile << "label=\"" << EdgeTypeStr[S.second] << "\"";
            DotFile << "];\n";
        }
    }
    DotFile << "}\n";


    // Path Counts

    for(auto &KV : AltCFG) {

        auto *BB = KV.first;
        auto &Succs = KV.second;
        APInt pathCount(256, StringRef("0"), 10);

        if (isFunctionExiting(BB))
            pathCount = 1;

        for (auto &S : Succs) {
            auto *SB = S.first;
            auto ET = S.second;
            Val.insert(make_pair(Edge::makeEdge(BB, SB, ET), pathCount));
            if (numPaths.count(SB) == 0)
                numPaths.insert(make_pair(SB, APInt(256, StringRef("0"), 10)));

            pathCount += numPaths[SB];
        }
        numPaths.insert(make_pair(BB, pathCount));
    }

    DEBUG(errs() << "\nEdge Weights :\n");
    for (auto &V : Val)
        DEBUG(errs() << V.first->src()->getName() << " -> "
                     << V.first->tgt()->getName() << " " << V.second << "\n");

    DEBUG(errs() << "\nPath Counts :\n");
    for (auto &P : numPaths)
        DEBUG(errs() << P.first->getName() << " -> " << P.second << "\n");


    auto T = getSpanningTree(AltCFG, Val);

    DEBUG(errs() << "\nSpanning Tree :\n");
    for (auto &E : T)
        DEBUG(errs() << E->src()->getName() << " -> " << E->tgt()->getName()
                     << "\n");

    auto Chords = getChords(Val, T);

    DEBUG(errs() << "\nChords :\n");
    for (auto &C : Chords)
        DEBUG(errs() << C->src()->getName() << " -> " << C->tgt()->getName()
                     << "\n");

    computeIncrement(Entry, Exit, Inc, Val, T, Chords);

    DEBUG(errs() << "\nVals :\n");
    for (auto &V : Val)
        DEBUG(errs() << V.first->src()->getName() << " -> "
                     << V.first->tgt()->getName() << " " << V.second << "\n");

    DEBUG(errs() << "\nIncrements :\n");
    for (auto &I : Inc)
        DEBUG(errs() << I.first->src()->getName() << " -> "
                     << I.first->tgt()->getName() << " " << I.second << "\n");

    // DEBUG(errs() << "NumPaths : " << numPaths[&F.getEntryBlock()] << "\n");
    errs() << "NumPaths : " << numPaths[Entry] << "\n";
}

char EPPEncode::ID = 0;
static RegisterPass<EPPEncode> X("", "PASHA - EPPEncode");
