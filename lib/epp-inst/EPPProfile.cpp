#define DEBUG_TYPE "epp_profile"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include "Common.h"
#include "EPPEncode.h"
#include "EPPProfile.h"
#include "AltCFG.h"
#include <cassert>
#include <tuple>
#include <unordered_map>

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
    auto &Ctx = module.getContext();

    for (auto &func : module) {
        if (isTargetFunction(func, FunctionList)) {
            LI = &getAnalysis<LoopInfoWrapperPass>(func).getLoopInfo();
            auto &enc = getAnalysis<EPPEncode>(func);
            assert(!hasRecursiveCall(func) &&
                    "Pathprofiling is not implemented for recursive functions");
            instrument(func, enc);
        }
    }

    auto *voidTy = Type::getVoidTy(Ctx);
    auto *printer =
        module.getOrInsertFunction("PaThPrOfIlInG_save", voidTy, nullptr);
    appendToGlobalDtors(module, llvm::cast<Function>(printer), 0);

    return true;
}

//static bool isFunctionExiting(BasicBlock *BB) {
//return true;
//return false;
//}

static SmallVector<BasicBlock*, 1>
getFunctionExitBlocks(Function &F) {
    SmallVector<BasicBlock*, 1> R;
    for(auto &BB : F) {
        if (dyn_cast<ReturnInst>(BB.getTerminator())) {
            R.push_back(&BB);
        }
    }
    return R;
}

void EPPProfile::instrument(Function &F, EPPEncode &Enc) {
    Module *M = F.getParent();
    auto &Ctx = M->getContext();
    auto *voidTy = Type::getVoidTy(Ctx);

    auto *CtrTy = Type::getInt128Ty(Ctx);
    auto *logFun2 = M->getOrInsertFunction("PaThPrOfIlInG_logPath2", voidTy,
            CtrTy, nullptr);

    auto *Ctr = new AllocaInst(CtrTy, nullptr, "epp.ctr",
            &*F.getEntryBlock().getFirstInsertionPt());

    auto *SI = new StoreInst(
            ConstantInt::getIntegerValue(CtrTy, APInt(128, 0, true)), Ctr);
    SI->insertAfter(Ctr);

    auto InsertInc = [&Ctr, &CtrTy](Instruction *addPos,
            APInt Increment) {
        if (Increment.ne(APInt(128, 0, true))) {
            DEBUG(errs() << "Inserting Increment " << Increment << " "
                         << addPos->getParent()->getName() << "\n");
            // Context Counter
            auto *LI = new LoadInst(Ctr, "ld.epp.ctr", addPos);
            auto *BI = BinaryOperator::CreateAdd(
                    LI, ConstantInt::getIntegerValue(CtrTy, Increment));
            BI->insertAfter(LI);
            (new StoreInst(BI, Ctr))->insertAfter(BI);
        }
    };

    auto InsertLogPath = [&logFun2, &Ctr, &CtrTy](BasicBlock *BB) {
        auto logPos = BB->getTerminator();
        auto *LI = new LoadInst(Ctr, "ld.epp.ctr", logPos);
        auto *CI = CallInst::Create(logFun2, {LI}, "");
        CI->insertAfter(LI);
        (new StoreInst(ConstantInt::getIntegerValue(CtrTy, APInt(128, 0, true)),
                       Ctr))->insertAfter(CI);
    };

    auto blockIndex = [](const PHINode *Phi, const BasicBlock *BB) -> uint32_t {
        for (uint32_t I = 0; I < Phi->getNumIncomingValues(); I++) {
            if (Phi->getIncomingBlock(I) == BB)
                return I;
        }
        assert(false && "Unreachable");
    };

    auto patchPhis = [&blockIndex](BasicBlock *Src, BasicBlock *Tgt,
            BasicBlock *New) {
        for (auto &I : *Tgt) {
            if (auto *Phi = dyn_cast<PHINode>(&I)) {
                Phi->setIncomingBlock(blockIndex(Phi, Src), New);
            }
        }
    };

    auto interpose = [&Ctx, &patchPhis](BasicBlock *Src,
            BasicBlock *Tgt) -> BasicBlock * {
        DEBUG(errs() << "Split : " << Src->getName() << " " << Tgt->getName()
                << "\n");
        // Sanity Checks
        auto found = false;
        for (auto S = succ_begin(Src), E = succ_end(Src); S != E; S++)
            if (*S == Tgt)
                found = true;
        assert(found && "Could not find the edge to split");

        auto *F = Tgt->getParent();
        auto *BB = BasicBlock::Create(Ctx, Src->getName() + ".intp", F);
        patchPhis(Src, Tgt, BB);
        auto *T = Src->getTerminator();
        T->replaceUsesOfWith(Tgt, BB);
        BranchInst::Create(Tgt, BB);
        return BB;
    };

    auto ExitBlocks = getFunctionExitBlocks(F);
    auto *Entry = &F.getEntryBlock(), *Exit = *ExitBlocks.begin(); 

    altepp::CFGInstHelper Inst(Enc.test, Entry, Exit);

    APInt BackVal = APInt(128, 0, true);
#define _ std::ignore 
    tie(_, BackVal, _, _)= Inst.get({Exit, Entry});
#undef _

    DEBUG(errs() << "BackVal : " <<  BackVal.toString(10, true) << "\n");

    SmallVector<altepp::Edge, 32> FunctionEdges;
    // For each edge in the function, get the increments 
    // for the edge and stick them in there.
    for(auto &BB : F) {
        for(auto SB = succ_begin(&BB), SE = succ_end(&BB); 
                SB != SE; SB++) {
            FunctionEdges.push_back({&BB, *SB});                         
        }
    }    

    for(auto &E : FunctionEdges) {
        APInt Val1(128, 0, true), Val2(128, 0, true);
        bool Exists = false, Log = false;
        tie(Exists, Val1, Log, Val2) = Inst.get(E);

        if(Exists) {
            auto *Split = interpose(SRC(E), TGT(E));
            if(Log) {
                DEBUG(errs() << "Val1 : " << Val1.toString(10, true) << "\n");
                DEBUG(errs() << "Val2 : " << Val2.toString(10, true) << "\n");
                InsertInc(&*Split->getFirstInsertionPt(), Val1 + BackVal);
                InsertLogPath(Split);
                InsertInc(Split->getTerminator(), Val2);
            } else {
                DEBUG(errs() << "Val1 : " << Val1.toString(10, true) << "\n");
                InsertInc(&*Split->getFirstInsertionPt(), Val1);
            }
        }
    }

    // Add the logpath function for all function exiting
    // basic blocks.
    for(auto &EB : ExitBlocks) {
        InsertLogPath(EB);
    }

#if 0
    // Maps for all the *special* inc values for loops
    unordered_map<Loop *, pair<APInt, APInt>> LatchMap;
    unordered_map<Loop *, pair<BasicBlock *, APInt>> InMap;

    typedef pair<BasicBlock *, BasicBlock *> Key;
    typedef pair<APInt, APInt> Val;
    auto PairCmp = [](const Key &A, const Key &B) -> bool {
        return A.first < B.first || (A.first == B.first && A.second < B.second);
    };
    map<Key, Val, decltype(PairCmp)> OutMap(PairCmp);

    APInt BackVal(128, 0, true);

    auto Loops = common::getLoops(LI);

    // Init Maps
    for (auto *L : Loops) {
        LatchMap[L] = make_pair(APInt(128, 0, true), APInt(128, 0, true));
        InMap[L] = make_pair(L->getLoopPreheader(), APInt(128, 0, true));
        SmallVector<pair<const BasicBlock *, const BasicBlock *>, 4> ExitEdges;
        L->getExitEdges(ExitEdges);
        for (auto &E : ExitEdges) {
            DEBUG(errs() << E.first->getName() << " -- " << E.second->getName()
                    << "\n");
            auto *Src = const_cast<BasicBlock *>(E.first);
            auto *Tgt = const_cast<BasicBlock *>(E.second);
            OutMap.insert(
                    make_pair(make_pair(Src, Tgt),
                        make_pair(APInt(128, 0, true), APInt(128, 0, true))));
        }
    }

    auto getFirstPred = [](BasicBlock *EB) -> BasicBlock * {
        return *pred_begin(EB);
    };

    // Populate Maps
    bool found = false;
    for (auto &KV : Enc.Inc) {
        shared_ptr<Edge> E(KV.first);
        auto &X = KV.second;

        if (E->Type == EOUT) {
            BackVal = KV.second;
            found = true;
        } else if (E->Type == ELATCH) {
            auto *L = LI->getLoopFor(E->src());
            assert(L && "Loop not found for ELATCH");
            LatchMap[L].second = X;
        } else if (E->Type == EHEAD) {
            auto *L = LI->getLoopFor(E->tgt());
            assert(L && "Loop not found for EHEAD");
            LatchMap[L].first = X;
        } else if (E->Type == ELIN) {
            auto *T = E->src()->getTerminator();
            assert(T->getNumSuccessors() == 1 &&
                    "Should be 1 guaranteed by LoopSimplify");
            auto *L = LI->getLoopFor(T->getSuccessor(0));
            assert(L && "Loop not found for ELIN");
            InMap[L].second = X;
        } else if (E->Type == ELOUT1) {
            auto *L = LI->getLoopFor(E->src());
            assert(L && "Loop not found for ELOUT1");
            for (auto &KV : OutMap) {
                if (KV.first.first == E->src())
                    KV.second.first = X;
            }
        } else if (E->Type == ELOUT2) {
            // Getting any predecessor works because all loop
            // exit blocks are guaranteed to be dominated by
            // the loop header.
            auto *L = LI->getLoopFor(getFirstPred(E->tgt()));
            assert(L && "Loop not found for ELOUT2");
            for (auto &KV : OutMap) {
                if (KV.first.second == E->tgt())
                    KV.second.second = X;
            }
        }
    }
    assert(found && "BackVal not found");
    DEBUG(errs() << "BackVal " << BackVal << "\n");

    // Split Edges and insert increments for all
    // real edges as well as last exit increment
    DEBUG(errs() << "EREAL and EOUT\n");
    for (auto &I : Enc.Inc) {
        shared_ptr<Edge> E(I.first);
        auto &X = I.second;
        if (E->Type == EREAL && X.ne(APInt(128, 0, true))) {
            // errs() << "Splitting Real Edge\n";
            auto NewBlock = interpose(E->src(), E->tgt());
            InsertInc(NewBlock->getFirstNonPHI(), X);
        } else if (E->Type == EOUT) {
            InsertInc(E->src()->getFirstNonPHI(), X);
        }
    }

    // Insert increments for all latches
    // This destroys LoopInfo so don't use that anymore
    DEBUG(errs() << "ELATCH\n");
    for (auto &L : LatchMap) {
        // Loop Latch
        auto Latch = L.first->getLoopLatch();
        // errs() << "Splitting Latch\n";
        assert(Latch && "More than one loop latch exists");
        auto SplitLatch = interpose(Latch, L.first->getHeader());
        InsertInc(SplitLatch->getFirstNonPHI(), L.second.first + BackVal);
        InsertLogPath(SplitLatch);
        InsertInc(SplitLatch->getTerminator(), L.second.second);
    }

    // Insert increments for all loop entry
    DEBUG(errs() << "ELIN\n");
    for (auto &KV : InMap) {
        Loop *L = KV.first;
        auto &V = KV.second;
        auto *PreHeader = V.first;
        auto *Header = L->getHeader();
        assert(Header == PreHeader->getTerminator()->getSuccessor(0) &&
                "Should be guaranteed by Loop Simplify");
        auto *NPH = interpose(PreHeader, Header);
        InsertInc(NPH->getFirstNonPHI(), V.second + BackVal);
        InsertLogPath(NPH);
        InsertInc(NPH->getTerminator(), LatchMap[L].second);
    }

    DEBUG(errs() << "ELOUT\n");
    for (auto &KV : OutMap) {
        auto *Src = KV.first.first, *Tgt = KV.first.second;
        auto A = KV.second.first, B = KV.second.second;
        auto *N = interpose(Src, Tgt);
        InsertInc(N->getTerminator(), A + BackVal);
        InsertLogPath(N);
        InsertInc(N->getTerminator(), B);
    }

#endif

    //for (auto &BB : F)
    //if (isFunctionExiting(&BB))
    //InsertLogPath(&BB);
}

char EPPProfile::ID = 0;
static RegisterPass<EPPProfile> X("", "PASHA - EPPProfile");
