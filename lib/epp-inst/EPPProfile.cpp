#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "EPPEncode.h"
#include "EPPProfile.h"
#include <unordered_map>
#include <cassert>

#define DEBUG_TYPE "epp_profile"

using namespace llvm;
using namespace epp;
using namespace std;

extern cl::list<std::string> FunctionList;
extern bool isTargetFunction(const Function &f,
                             const cl::list<std::string> &FunctionList);

bool EPPProfile::doInitialization(Module &m) {
    assert(FunctionList.size() == 1 &&
           "Only one function can be marked for profiling");
    return false;
}

bool EPPProfile::doFinalization(Module &m) { return false; }

bool hasRecursiveCall(Function &F) {
    for (auto &BB : F) {
        for (auto &I : BB) {
            CallSite CS(&I);
            if (CS.isCall() || CS.isInvoke()) {
                if (CS.getCalledFunction() == &F)
                    return true;
            }
        }
    }
    return false;
}

bool EPPProfile::runOnModule(Module &module) {
    DEBUG(errs() << "Running Profile\n");
    auto &context = module.getContext();

    for (auto &func : module) {
        if (isTargetFunction(func, FunctionList)) {
            LI = &getAnalysis<LoopInfo>(func);
            auto &enc = getAnalysis<EPPEncode>(func);
            assert(!hasRecursiveCall(func) &&
                   "Pathprofiling is not implemented for recursive functions");
            instrument(func, enc);
        }
    }

    auto *voidTy = Type::getVoidTy(context);
    auto *printer =
        module.getOrInsertFunction("PaThPrOfIlInG_save", voidTy, nullptr);
    appendToGlobalDtors(module, llvm::cast<Function>(printer), 0);

    return true;
}

static bool isFunctionExiting(BasicBlock *BB) {
    if (dyn_cast<ReturnInst>(BB->getTerminator()))
        return true;

    // TODO: Check for exception handling
    // TODO: Check for functions that don't return

    return false;
}

static BasicBlock *SplitEdgeWrapper(BasicBlock *Src, BasicBlock *Tgt,
                                    ModulePass *Ptr) {
    for (auto S = succ_begin(Src), E = succ_end(Src); S != E; S++)
        if (*S == Tgt)
            return SplitEdge(Src, Tgt, Ptr);

    assert(false && "SplitEdge bug not solved by MST optimization");
}

void EPPProfile::instrument(Function &F, EPPEncode &Enc) {
    Module *M = F.getParent();
    auto &context = M->getContext();
    auto *int64Ty = Type::getInt64Ty(context);
    auto *voidTy = Type::getVoidTy(context);
    auto *incFun = M->getOrInsertFunction("PaThPrOfIlInG_incCount", voidTy, int64Ty,
                               int64Ty, int64Ty, int64Ty, nullptr);
    auto *logFun = M->getOrInsertFunction("PaThPrOfIlInG_logPath", voidTy, nullptr);
    auto *selfLoopFun = M->getOrInsertFunction("PaThPrOfIlInG_selfLoop", voidTy,
                                               int64Ty, nullptr);

    auto InsertInc = [&incFun, &int64Ty](Instruction *addPos, APInt Increment) {
        DEBUG(errs() << "Inserting Increment " << Increment << " "
                     << addPos->getParent()->getName() << "\n");
        auto *I = Increment.getRawData();
        vector<Value *> Args;
        for(uint32_t C = 0; C < 4; C++)
            Args.push_back(ConstantInt::get(int64Ty, I[C], false));
        CallInst::Create(incFun, Args, "", addPos);
    };

    auto InsertLogPath = [&logFun](BasicBlock *BB) {
        auto logPos = BB->getTerminator();
        CallInst::Create(logFun, "", logPos);
    };

    auto loopExitSplit = [&context](BasicBlock* Tgt) -> BasicBlock* {
        auto *BB = BasicBlock::Create(context, "lexit.split", Tgt->getParent());
        auto *Src = Tgt->getUniquePredecessor();
        // FIXME : Condition is that loop header dominates exit. So there could
        // be 2 or more preds where the preds are also dominated by header or
        // may even be the header itself.
        assert(Src && "Should be unique -- guaranteed by loopSimplify");
        Src->replaceSuccessorsPhiUsesWith(BB);
        auto *T = Src->getTerminator();
        T->replaceUsesOfWith(Tgt, BB);
        BranchInst::Create(Tgt, BB);
        return BB;
    };

    auto loopEntrySplit = [&context](BasicBlock* Src) -> BasicBlock* {
        auto *BB = BasicBlock::Create(context, "lentry.split", Src->getParent());
        auto *Tgt = Src->getTerminator()->getSuccessor(0);
        assert(Src->getTerminator()->getNumSuccessors() == 1 && 
                "Should be unique -- guaranteed by loopSimplify");
        Src->replaceSuccessorsPhiUsesWith(BB);
        auto *T = Src->getTerminator();
        T->replaceUsesOfWith(Tgt, BB);
        BranchInst::Create(Tgt, BB);
        return BB;
    };

    // Maps for all the *special* inc values for loops
    unordered_map<Loop *, pair<APInt, APInt>> LatchMap;
    unordered_map<Loop *, pair<BasicBlock*, APInt>> InMap;
    unordered_map<Loop *, unordered_map<BasicBlock*, APInt>> OutMap;
    APInt BackVal(256, 0, true);

    SmallVector<Loop* , 16> Loops;
    for(auto &L : *LI) {
        Loops.push_back(L);
        assert(L->getLoopDepth() == 1 && 
                "Expect only top level loops here");
        for(auto &SL : L->getSubLoops()) {
           Loops.push_back(SL); 
        }
    }

    // Init Maps
    for(auto *L : Loops) {
        LatchMap[L] = make_pair(APInt(256, 0, true), APInt(256, 0, true));
        InMap[L] = make_pair(L->getLoopPreheader(),APInt(256, 0, true));
        SmallVector<BasicBlock*, 4> EBs;
        L->getUniqueExitBlocks(EBs);
        for(auto &BB : EBs)
            OutMap[L].insert(make_pair(BB, APInt(256, 0, true)));
    }

    // Populate Maps
    for(auto &KV : Enc.Inc) {
        shared_ptr<Edge> E(KV.first);
        auto &X = KV.second;

        if(E->Type == EOUT) {
            BackVal = KV.second;
        } else if (E->Type == ELATCH) {
            auto *L = LI->getLoopFor(E->src());
            assert(L);
            LatchMap[L].second = X;
        } else if (E->Type == EHEAD) {
            auto *L = LI->getLoopFor(E->tgt());
            assert(L);
            LatchMap[L].first = X;
        } else if (E->Type == ELIN) {
            auto *T = E->src()->getTerminator();
            assert(T->getNumSuccessors() == 1 && 
                    "Should be 1 guaranteed by LoopSimplify");
            auto *L = LI->getLoopFor(T->getSuccessor(0));
            assert(L);
            InMap[L].second =  X; 
        } else if (E->Type == ELOUT) {
            auto *L = LI->getLoopFor(E->tgt()->getUniquePredecessor());
            assert(L);
            OutMap[L][E->tgt()] = X;
        }
    }

    // Split Edges and insert increments for all
    // real edges as well as last exit increment
    for (auto &I : Enc.Inc) {
        shared_ptr<Edge> E(I.first);
        auto &X = I.second;
        if (E->Type == EREAL) {
            auto NewBlock = SplitEdgeWrapper(E->src(), E->tgt(), this);
            InsertInc(NewBlock->getFirstNonPHI(), X);
        } else if (E->Type == EOUT) {
            InsertInc(E->src()->getFirstNonPHI(), X);
        }
    }

    // Insert increments for all latches
    // This destroys LoopInfo so don't use that anymore
    for (auto &L : LatchMap) {
        // Loop Latch
        auto Latch = L.first->getLoopLatch();
        assert(Latch && "More than one loop latch exists");
        auto SplitLatch = SplitEdgeWrapper(Latch, L.first->getHeader(), this);
        InsertInc(SplitLatch->getFirstNonPHI(), L.second.first + BackVal);
        InsertLogPath(SplitLatch);
        InsertInc(SplitLatch->getTerminator(), L.second.second);
    }

    // Add the counters for all self loops
    //auto InsertSelfLoop =
        //[&int64Ty, &selfLoopFun](BasicBlock *BB, std::uint64_t SelfLoopId) {
            //DEBUG(errs() << "Inserting SelfLoop Increment" << SelfLoopId << " "
                         //<< BB->getName() << "\n");
            //auto logPos = BB->getTerminator();
            //Value *args[] = {ConstantInt::get(int64Ty, SelfLoopId, false)};
            //CallInst::Create(selfLoopFun, args, "", logPos);
        //};

    //for (auto &KV : Enc.selfLoopMap) {
        //auto Id = KV.first;
        //auto BB = KV.second;
        //auto NewBB = SplitEdgeWrapper(BB, BB, this);
        //InsertSelfLoop(NewBB, Id);
    //}

    // Insert increments for all loop entry 
    for(auto &KV : InMap) {
        Loop* L = KV.first;
        auto &V = KV.second;
        auto *NPH = loopEntrySplit(V.first);
        InsertInc(NPH->getFirstNonPHI(), V.second + BackVal);
        InsertLogPath(NPH);
        InsertInc(NPH->getTerminator(), LatchMap[L].second);
    }

    // Insert increment for all loop exits
    for(auto &KV : OutMap) {
        Loop* L = KV.first;
        for(auto &KV2 : KV.second) {
            auto &V = KV2.second;
            auto *BB = loopExitSplit(KV2.first);
            InsertInc(BB->getFirstNonPHI(), LatchMap[L].first + BackVal);
            InsertLogPath(BB);
            InsertInc(BB->getTerminator(), V);
        }
    }

    // Add the logpath function for all function exiting
    // basic blocks.
    for (auto &BB : F)
        if (isFunctionExiting(&BB))
            InsertLogPath(&BB);
}

char EPPProfile::ID = 0;
static RegisterPass<EPPProfile> X("","PASHA - EPPProfile");
