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

    auto InsertLogPath2 = [&logFun](BasicBlock *BB) {
        auto logPos = BB->getFirstInsertionPt();
        CallInst::Create(logFun, "", logPos);
    };

    auto loopExitSplit = [&context](BasicBlock* Tgt) -> BasicBlock* {
        auto *BB = BasicBlock::Create(context, "lexit.split", Tgt->getParent());
        auto *Src = Tgt->getUniquePredecessor();
        assert(Src && "Should be unique -- guaranteed by loopSimplify");
        auto *T = Src->getTerminator();
        T->replaceUsesOfWith(Tgt, BB);
        //T->setSuccessor(GetSuccessorNumber(Tgt, Src), BB);
        BranchInst::Create(Tgt, BB);
        return BB;
    };

    APInt BackVal(256, 0, true);
    for(auto &KV : Enc.Inc) {
        if(KV.first->Type == EOUT) 
            BackVal = KV.second;
    }

    unordered_map<Loop *, pair<APInt, APInt>> LoopMap;
    for(auto *L : *LI) {
        LoopMap[L] = make_pair(APInt(256, 0, true), APInt(256, 0, true));
    } 

    for (auto &I : Enc.Inc) {
        shared_ptr<Edge> E(I.first);
        auto &X = I.second;

        if (E->Type == EREAL) {
            auto NewBlock = SplitEdgeWrapper(E->src(), E->tgt(), this);
            InsertInc(NewBlock->getFirstNonPHI(), X);
        } else if (E->Type == EOUT) {
            InsertInc(E->src()->getFirstNonPHI(), X);
        } else if (E->Type == EHEAD) {
            auto *L = LI->getLoopFor(E->tgt());
            LoopMap[L].first = X;
        } else if (E->Type == ELATCH) {
            auto *L = LI->getLoopFor(E->src());
            LoopMap[L].second = X;
        } else if (E->Type == ELIN) {
            auto *PH = E->src();            
            //InsertLogPath(PH);
            InsertInc(PH->getTerminator(), X + BackVal);
        } else if (E->Type == ELOUT) {
            auto NewBlock = loopExitSplit(E->tgt());
            //InsertLogPath(NewBlock);
            InsertInc(NewBlock->getTerminator(), X);
        }
    }

    for (auto &L : LoopMap) {
        // Preheader
        InsertLogPath2(L.first->getLoopPreheader());
        // Loop exits
        SmallVector<BasicBlock*, 4> EBs;
        L.first->getExitBlocks(EBs);
        for(auto &BB : EBs)
            InsertLogPath2(BB);
        auto Latch = L.first->getLoopLatch();
        assert(Latch && "More than one loop latch exists");
        auto SplitLatch = SplitEdgeWrapper(Latch, L.first->getHeader(), this);
        InsertInc(SplitLatch->getFirstNonPHI(), L.second.first + BackVal);
        InsertLogPath(SplitLatch);
        InsertInc(SplitLatch->getTerminator(), L.second.second);
    }

    // Add the counters for all self loops
    auto InsertSelfLoop =
        [&int64Ty, &selfLoopFun](BasicBlock *BB, std::uint64_t SelfLoopId) {
            DEBUG(errs() << "Inserting SelfLoop Increment" << SelfLoopId << " "
                         << BB->getName() << "\n");
            auto logPos = BB->getTerminator();
            Value *args[] = {ConstantInt::get(int64Ty, SelfLoopId, false)};
            CallInst::Create(selfLoopFun, args, "", logPos);
        };

    for (auto &KV : Enc.selfLoopMap) {
        auto Id = KV.first;
        auto BB = KV.second;
        auto NewBB = SplitEdgeWrapper(BB, BB, this);
        InsertSelfLoop(NewBB, Id);

    }

    // Add the logpath function for all function exiting
    // basic blocks.
    for (auto &BB : F)
        if (isFunctionExiting(&BB))
            InsertLogPath(&BB);
}

char EPPProfile::ID = 0;
static RegisterPass<EPPProfile> X("peruse-epp-profile",
                                  "Efficient Path Profiling -- Profile");
