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

// cl::list<std::string> FunctionList("epp-fn", cl::value_desc("String"),
// cl::desc("List of functions to instrument"),
// cl::OneOrMore, cl::CommaSeparated);

// bool isTargetFunction(const Function &f,
// const cl::list<std::string> &FunctionList) {
// if (f.isDeclaration())
// return false;
// for (auto &fname : FunctionList)
// if (fname == f.getName())
// return true;
// return false;
//}

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
    auto *incFun =
        M->getOrInsertFunction("PaThPrOfIlInG_incCount", voidTy, int64Ty,
                               int64Ty, int64Ty, int64Ty, nullptr);

    auto *logFun =
        M->getOrInsertFunction("PaThPrOfIlInG_logPath", voidTy, nullptr);

    auto *selfLoopFun = M->getOrInsertFunction("PaThPrOfIlInG_selfLoop", voidTy,
                                               int64Ty, nullptr);

    auto InsertInc = [&incFun, &int64Ty](Instruction *addPos, APInt Increment) {
        DEBUG(errs() << "Inserting Increment " << Increment << " "
                     << addPos->getParent()->getName() << "\n");
        auto *I = Increment.getRawData();
        vector<Value *> Args;
        Args.push_back(ConstantInt::get(int64Ty, I[0], false));
        Args.push_back(ConstantInt::get(int64Ty, I[1], false));
        Args.push_back(ConstantInt::get(int64Ty, I[2], false));
        Args.push_back(ConstantInt::get(int64Ty, I[3], false));
        CallInst::Create(incFun, Args, "", addPos);

    };

    auto InsertLogPath = [&logFun](BasicBlock *BB) {
        // DEBUG(errs() << "Inserting LogFunction " << FunctionID << " " <<
        // BB->getName() << "\n");
        auto logPos = BB->getTerminator();
        CallInst::Create(logFun, "", logPos);
    };

    auto InsertSelfLoop =
        [&int64Ty, &selfLoopFun](BasicBlock *BB, std::uint64_t SelfLoopId) {
            DEBUG(errs() << "Inserting SelfLoop Increment" << SelfLoopId << " "
                         << BB->getName() << "\n");
            auto logPos = BB->getTerminator();
            Value *args[] = {ConstantInt::get(int64Ty, SelfLoopId, false)};
            CallInst::Create(selfLoopFun, args, "", logPos);
        };

    APInt BackVal(256, StringRef("0"), 10);
    unordered_map<Loop *, pair<APInt, APInt>> LoopMap;

    for (auto &I : Enc.Inc) {
        if (I.second == 0)
            continue;

        shared_ptr<Edge> E(I.first);

        if (E->Type == EREAL) {
            // Real Edge instrumentation,
            // Don't split if you don't have to -
            // this also avoids a bug in the case where
            // A->B needs to be split and B->C needs to be
            // split. If A->B is split before B->C and the
            // split occurs at B, then the saved Edge B->C
            // contains invalid pointer.
            // TODO : Revisit this issue
            auto NewBlock = SplitEdgeWrapper(E->src(), E->tgt(), this);
            InsertInc(NewBlock->getFirstNonPHI(), I.second);
        } else if (E->Type == EOUT) {
            // Final exit counter increment
            // Just stick it in the Exiting BB (E->src())

            InsertInc(E->src()->getFirstNonPHI(), I.second);
            BackVal = I.second;
        } else {
            // Fake edge for loops
            // a) Entry to Header
            // b) Latch to Header
            // Collect the info here,
            // instrument it later.

            Loop *L = LI->getLoopFor(E->src());

            if (L) {
                // Latch to Header
                if (LoopMap.count(L))
                    LoopMap[L].first = I.second;
                else
                    LoopMap.insert(make_pair(
                        L,
                        make_pair(I.second, APInt(256, StringRef("0"), 10))));
            } else {
                // Entry to Header
                L = LI->getLoopFor(E->tgt());
                if (LoopMap.count(L))
                    LoopMap[L].second = I.second;
                else
                    LoopMap.insert(
                        make_pair(L, make_pair(APInt(256, StringRef("0"), 10),
                                               I.second)));
            }
        }
    }

    // Add the Fake OUT and Fake IN edge increments respectively
    // to the split latch basic block with the path logging
    // in the middle.
    for (auto &L : LoopMap) {
        auto Latch = L.first->getLoopLatch();
        assert(Latch && "More than one loop latch exists");

        auto SplitLatch = SplitEdgeWrapper(Latch, L.first->getHeader(), this);
        InsertInc(SplitLatch->getFirstNonPHI(), L.second.first + BackVal);
        InsertLogPath(SplitLatch);
        InsertInc(SplitLatch->getTerminator(), L.second.second);
    }

    // Add the counters for all self loops
    for (auto &KV : Enc.selfLoopMap) {
        auto Id = KV.first;
        auto BB = KV.second;
        auto NewBB = SplitEdgeWrapper(BB, BB, this);
        InsertSelfLoop(NewBB, Id);

        // Ideally, we want to distinguish the incoming path,
        // and the outgoing path as separate paths, which occur
        // before and after the self loop invocation. This is however,
        // very painful and I don' want to do it now. The code below
        // doesn't work since some self loops (hmmer- P7Viterbi) does not
        // have a preheader, even after loop-simplify.

        // BasicBlock *PreHeaderBB = nullptr;
        // for(auto P = pred_begin(BB), E = pred_end(BB); P != E; P++)
        //     if((*P)->getName().count("preheader"))
        //         PreHeaderBB = *P;

        // if(!PreHeaderBB)
        // {
        //     // FIXME : Debug this sometime
        //     // Dump the dot format CFG for the function which
        //     // has this dumbass self loop
        // }
        // assert(PreHeaderBB && "Could not find preheader, need to run
        // loop-simplify");
        // InsertLogPath(PreHeaderBB, Id);
        // auto Zap = ConstantInt::get(CounterTy, 0, false);
        // new StoreInst(Zap, Counter, PreHeaderBB->getTerminator());
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
