#include "MicroWorkloadExtract.h"
#include <boost/algorithm/string.hpp>
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include "llvm/IR/Verifier.h"
#include "llvm/PassManager.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/DenseSet.h"
#include <cxxabi.h>

#include <map>
#include <set>
#define DEBUG_TYPE "mw"

using namespace llvm;
using namespace mwe;
using namespace std;

//extern cl::opt<string> TargetFunction;

//cl::opt<int> MaxNumPaths("max", cl::desc("Maximum number of paths to analyse"),
                         //cl::value_desc("Integer"), cl::init(10));

extern cl::list<std::string> FunctionList;
extern bool isTargetFunction(const Function &f,
                      const cl::list<std::string> &FunctionList);

void MicroWorkloadExtract::readSequences(vector<Path> &S,
                                         map<int64_t, int64_t> &SM) {
    ifstream SeqFile(SeqFilePath.c_str(), ios::in);
    assert(SeqFile.is_open() && "Could not open file");
    string Line;
    for (int64_t Count = 0; getline(SeqFile, Line);) {
        Path P;
        std::vector<std::string> Tokens;
        boost::split(Tokens, Line, boost::is_any_of("\t "));
        P.Id = Tokens[0];

        P.Freq = stoull(Tokens[1]);
        P.PType = static_cast<PathType>(stoi(Tokens[2]));

        // Token[3] is the number of instructions in path.
        // Not needed here. It's there in the file for filtering
        // and finding out exactly how many paths we want to
        // analyse.

        // a. The last token is blank, so always end -1
        // b. If Path is RIRO, then range is +4, -1
        // c. If Path is FIRO, then range is +5, -1
        // d. If Path is RIFO, then range is +4, -2
        // e. If Path is FIFO, then range is +5, -2
        // f. If Path is SELF, then range is +4, -0
        switch (P.PType) {
        case RIRO:
            move(Tokens.begin() + 4, Tokens.end() - 1, back_inserter(P.Seq));
            break;
        case FIRO:
            move(Tokens.begin() + 5, Tokens.end() - 1, back_inserter(P.Seq));
            break;
        case RIFO:
            move(Tokens.begin() + 4, Tokens.end() - 2, back_inserter(P.Seq));
            break;
        case FIFO:
            move(Tokens.begin() + 5, Tokens.end() - 2, back_inserter(P.Seq));
            break;
        case SELF:
            move(Tokens.begin() + 4, Tokens.end() - 1, back_inserter(P.Seq));
            break;
        default:
            assert(false && "Unknown path type");
        }
        S.push_back(P);
        // SM[P.Id] = Count;
        Count++;
        if (Count == NumSeq)
            break;
    }
    SeqFile.close();
}

bool MicroWorkloadExtract::doInitialization(Module &M) {
    readSequences(Sequences, SequenceMap);
    return false;
}

bool MicroWorkloadExtract::doFinalization(Module &M) { return false; }

static bool isBlockInPath(const string &S, const Path &P) {
    return find(P.Seq.begin(), P.Seq.end(), S) != P.Seq.end();
}

// static inline bool isUnconditionalBranch(Instruction *I) {
//     if (auto BRI = dyn_cast<BranchInst>(I))
//         return BRI->isUnconditional();
//     return false;
// }
//
// static inline uint32_t getBlockIdx(const Path &P, BasicBlock *BB) {
//     auto Name = BB->getName().str();
//     uint32_t Idx = 0;
//     for (; Idx < P.Seq.size(); Idx++)
//         if (Name == P.Seq[Idx])
//             return Idx;
//     assert(false && "Should be Unreachable");
// }

static inline void bSliceDFSHelper(
    BasicBlock *BB, DenseSet<BasicBlock *> &BSlice,
    DenseSet<pair<const BasicBlock *, const BasicBlock *>> &BackEdges) {
    BSlice.insert(BB);
    for (auto PB = pred_begin(BB), PE = pred_end(BB); PB != PE; PB++) {
        if (!BSlice.count(*PB) && BackEdges.count(make_pair(*PB, BB)) == 0)
            bSliceDFSHelper(*PB, BSlice, BackEdges);
    }
}

static DenseSet<BasicBlock *>
bSliceDFS(BasicBlock *Begin,
          DenseSet<pair<const BasicBlock *, const BasicBlock *>> &BackEdges) {
    DenseSet<BasicBlock *> BSlice;
    bSliceDFSHelper(Begin, BSlice, BackEdges);
    return BSlice;
}

static inline void fSliceDFSHelper(
    BasicBlock *BB, DenseSet<BasicBlock *> &FSlice,
    DenseSet<pair<const BasicBlock *, const BasicBlock *>> &BackEdges) {
    FSlice.insert(BB);
    for (auto SB = succ_begin(BB), SE = succ_end(BB); SB != SE; SB++) {
        if (!FSlice.count(*SB) && BackEdges.count(make_pair(BB, *SB)) == 0)
            fSliceDFSHelper(*SB, FSlice, BackEdges);
    }
}

static DenseSet<BasicBlock *>
fSliceDFS(BasicBlock *Begin,
          DenseSet<pair<const BasicBlock *, const BasicBlock *>> &BackEdges) {
    DenseSet<BasicBlock *> FSlice;
    fSliceDFSHelper(Begin, FSlice, BackEdges);
    return FSlice;
}

static DenseSet<BasicBlock *>
getChop(BasicBlock *StartBB, BasicBlock *LastBB,
        DenseSet<pair<const BasicBlock *, const BasicBlock *>> &BackEdges) {

    DEBUG(errs() << "BackEdges : \n");
    for (auto &BE : BackEdges) {
        DEBUG(errs() << BE.first->getName() << "->" << BE.second->getName()
                     << "\n");
    }
    DEBUG(errs() << "------------------------------\n");

    // Compute Forward Slice from starting of path
    // Compute Backward Slice from last block in path
    // Compute the set intersection (chop) of these two
    // Construct an array ref of the blocks and hand off to the CodeExtractor

    DenseSet<BasicBlock *> FSlice = fSliceDFS(StartBB, BackEdges);
    DenseSet<BasicBlock *> BSlice = bSliceDFS(LastBB, BackEdges);

    DenseSet<BasicBlock *> Chop;
    for (auto &FB : FSlice)
        if (BSlice.count(FB))
            Chop.insert(FB);

    DEBUG(errs() << "Forward : " << StartBB->getName() << "\n");
    for (auto &F : FSlice) {
        DEBUG(errs() << F->getName() << "\n");
    }
    DEBUG(errs() << "------------------------------\n");

    DEBUG(errs() << "Backward : " << LastBB->getName() << "\n");
    for (auto &B : BSlice) {
        DEBUG(errs() << B->getName() << "\n");
    }
    DEBUG(errs() << "------------------------------\n");

    DEBUG(errs() << "Chop : \n");
    for (auto &C : Chop) {
        DEBUG(errs() << C->getName() << "\n");
    }
    DEBUG(errs() << "------------------------------\n");

    return Chop;
}

static void liveInHelperStatic(DenseSet<BasicBlock *> &Chop,
                               SmallVector<Value *, 16> &LiveIn,
                               DenseSet<Value *> &Globals, Value *Val) {
    if (auto Ins = dyn_cast<Instruction>(Val)) {
        for (auto OI = Ins->op_begin(), EI = Ins->op_end(); OI != EI; OI++) {
            if (auto OIns = dyn_cast<Instruction>(OI)) {
                if (!Chop.count(OIns->getParent())) {
                    LiveIn.push_back(OIns);
                }
            } else
                liveInHelperStatic(Chop, LiveIn, Globals, *OI);
        }
    } else if (auto CE = dyn_cast<ConstantExpr>(Val)) {
        for (auto OI = CE->op_begin(), EI = CE->op_end(); OI != EI; OI++) {
            assert(!isa<Instruction>(OI) &&
                   "Don't expect operand of ConstExpr to be an Instruction");
            liveInHelperStatic(Chop, LiveIn, Globals, *OI);
        }
    } else if (auto Arg = dyn_cast<Argument>(Val))
        LiveIn.push_back(Arg);
    else if (auto GV = dyn_cast<GlobalVariable>(Val))
        Globals.insert(GV);

    // Constants should just fall through and remain
    // in the trace.
}

void staticHelper(
    Function *StaticFunc, Function *GuardFunc, GlobalVariable *LOA,
    SmallVector<Value *, 16> &LiveIn, DenseSet<Value *> &LiveOut,
    DenseSet<Value *> &Globals, SmallVector<BasicBlock *, 16> &RevTopoChop,
    DenseSet<pair<const BasicBlock *, const BasicBlock *>> &BackEdges,
    LLVMContext &Context) {

    ValueToValueMapTy VMap;

    auto handleCallSites =
        [&VMap, &StaticFunc](CallSite &OrigCS, CallSite &StaticCS) {
            assert(OrigCS.getCalledFunction() &&
                   "We do not support indirect function calls in traces.");
            auto *FTy = OrigCS.getCalledFunction()->getFunctionType();
            auto *Val = OrigCS.getCalledValue();
            auto Name = OrigCS.getCalledFunction()->getName();
            if (VMap.count(Val) == 0) {
                Function *ExtFunc =
                    Function::Create(FTy, GlobalValue::ExternalLinkage, Name,
                                     StaticFunc->getParent());
                assert(VMap.count(Val) == 0 && "Need new values");
                VMap[Val] = static_cast<Value *>(ExtFunc);
            }
            StaticCS.setCalledFunction(VMap[Val]);
        };

    // Add Globals
    for (auto Val : Globals) {
        auto OldGV = dyn_cast<GlobalVariable>(Val);
        assert(OldGV && "Could not reconvert to Global Variable");
        GlobalVariable *GV = new GlobalVariable(
            *StaticFunc->getParent(), OldGV->getType()->getElementType(),
            OldGV->isConstant(), GlobalValue::ExternalLinkage,
            (Constant *)nullptr, OldGV->getName(), (GlobalVariable *)nullptr,
            OldGV->getThreadLocalMode(), OldGV->getType()->getAddressSpace());
        GV->copyAttributesFrom(OldGV);
        assert(VMap.count(OldGV) == 0 && "Need new values");
        VMap[OldGV] = GV;
    }

    for (auto IT = RevTopoChop.rbegin(), IE = RevTopoChop.rend(); IT != IE;
         IT++) {
        auto C = *IT;
        auto *NewBB = BasicBlock::Create(
            Context, StringRef("my_") + C->getName(), StaticFunc, nullptr);
        assert(VMap.count(C) == 0 && "Need new values");
        VMap[C] = NewBB;
        for (auto &I : *C) {
            if (find(LiveIn.begin(), LiveIn.end(), &I) != LiveIn.end())
                continue;
            auto *NewI = I.clone();
            NewBB->getInstList().push_back(NewI);

            assert(VMap.count(&I) == 0 && "Need new values");
            VMap[&I] = NewI;
            CallSite CS(&I), TraceCS(NewI);
            if (CS.isCall() || CS.isInvoke())
                handleCallSites(CS, TraceCS);
        }
    }

    // Assign names, if you don't have a name,
    // a name will be assigned to you.
    Function::arg_iterator AI = StaticFunc->arg_begin();
    uint32_t VReg = 0;
    for (auto Val : LiveIn) {
        auto Name = Val->getName();
        if (Name.empty())
            AI->setName(StringRef(string("vr.") + to_string(VReg++)));
        else
            AI->setName(Name + string(".in"));
        // VMap[Val] = AI;
        ++AI;
    }

    auto inChop = [&RevTopoChop](const BasicBlock *BB) -> bool {
        return (find(RevTopoChop.begin(), RevTopoChop.end(), BB) !=
                RevTopoChop.end());
    };

    auto rewriteUses =
        [&VMap, &RevTopoChop, &StaticFunc](Value *Val, Value *RewriteVal) {
            std::vector<User *> Users(Val->user_begin(), Val->user_end());
            for (std::vector<User *>::iterator use = Users.begin(),
                                               useE = Users.end();
                 use != useE; ++use) {
                if (Instruction *inst = dyn_cast<Instruction>(*use)) {
                    if (inst->getParent()->getParent() == StaticFunc) {
                        inst->replaceUsesOfWith(Val, RewriteVal);
                    }
                }
            }
        };

    AI = StaticFunc->arg_begin();
    // Patch instructions to arguments,
    for (auto Val : LiveIn) {
        Value *RewriteVal = AI++;
        rewriteUses(Val, RewriteVal);
    }

    for (auto IT = RevTopoChop.rbegin(), IE = RevTopoChop.rend(); IT != IE;
         IT++) {
        auto *BB = *IT;
        for (auto &I : *BB) {
            // If the original instruction has not been promoted to
            // a live-in, then rewrite it's users if they are in the
            // static function.
            if (find(LiveIn.begin(), LiveIn.end(), &I) != LiveIn.end())
                continue;
            std::vector<User *> Users(I.user_begin(), I.user_end());
            for (std::vector<User *>::iterator use = Users.begin(),
                                               useE = Users.end();
                 use != useE; ++use) {
                if (Instruction *inst = dyn_cast<Instruction>(*use)) {
                    if (inst->getParent()->getParent() == StaticFunc) {
                        assert(VMap[&I] && "Value not found in ValMap");
                        inst->replaceUsesOfWith(&I, VMap[&I]);
                    }
                }
            }
        }
    }

    function<void(Value&)> handleOperands;
    handleOperands = [&VMap, &handleOperands, &StaticFunc](Value& V) {
        User &I = *cast<User>(&V); 
        for (auto OI = I.op_begin(), E = I.op_end(); OI != E; ++OI) {
            if(auto CE = dyn_cast<ConstantExpr>(*OI)) {
                handleOperands(*CE);
            }
            if (auto *GV = dyn_cast<GlobalVariable>(*OI)) {
                // Since we may have already patched the global
                // don't try to patch it again.
                if (VMap.count(GV) == 0) continue;
                // Check if we came from a ConstantExpr
                // This represents a case where a ConstantExpr contains a reference to a GlobalVariable
                // Constants can be shared across modules, but GlobalVariables cannot. We need to
                // create a new ConstantExpr which refers to the new GlobalVariable.
                // The following example does *not* trigger the verifier if the GV is not 
                // remapped. 
                // %1 = getelementptr inbounds i32* getelementptr inbounds ([66 x i32]* @two_over_pi82, i32 0, i32 0), i32 %j.0352.i62.in
                // This expression gets optimized to :
                // %1 = getelementptr inbounds [66 x i32]* @two_over_pi82, i32 0, i32 %j.0352.i62.in
                // which is caught by the Verifier.
                if(auto CE = dyn_cast<ConstantExpr>(&V)) {
                    int32_t OpIdx = -1;
                    while(I.getOperand(++OpIdx) != GV);
                    auto NCE = CE->getWithOperandReplaced(OpIdx, 
                                         cast<Constant>(VMap[GV]));
                    for(auto UI = CE->user_begin(), UE = CE->user_end();
                            UI != UE; UI++) {
                        // All users of ConstExpr should be instructions
                        auto Ins = dyn_cast<Instruction>(*UI);
                        if( Ins->getParent()->getParent() == StaticFunc) {
                            Ins->replaceUsesOfWith(CE, NCE);
                        }
                    }             
                } else {
                    I.replaceUsesOfWith(GV, VMap[GV]);
                }
            }
        }
    };

    // Patch Globals
    for (auto &BB : *StaticFunc) {
        for (auto &I : BB) {
            handleOperands(I);
        }
    }

    // Patch branches

    auto *BB = cast<BasicBlock>(VMap[RevTopoChop.front()]);
    BB->getTerminator()->eraseFromParent();
    ReturnInst::Create(Context, BB);

    auto insertGuardCall =
        [&GuardFunc, &VMap](const BranchInst *CBR, BasicBlock *Blk) {
            Value *Arg = CBR->getCondition();
            auto CI = CallInst::Create(GuardFunc, {Arg}, "", Blk);
            // Add a ReadNone+NoInline attribute to the CallSite, which
            // will hopefully help the optimizer.
            CI->setDoesNotAccessMemory();
            CI->setIsNoInline();
        };

    BasicBlock *SwitchTgt = nullptr;

    uint64_t GuardChecks = 0;
    for (auto IT = next(RevTopoChop.begin()), IE = RevTopoChop.end(); IT != IE;
         IT++) {
        auto *NewBB = cast<BasicBlock>(VMap[*IT]);
        auto T = NewBB->getTerminator();

        // LLVM 3.7+
        // assert(!isa<CatchEndPadInst>(T) && "Not handled");
        // assert(!isa<CatchPadInst>(T) && "Not handled");
        // assert(!isa<CatchReturnInst>(T) && "Not handled");
        // assert(!isa<CleanupEndPadInst>(T) && "Not handled");
        // assert(!isa<CleanupReturnInst>(T) && "Not handled");
        // assert(!isa<TerminatePadInst>(T) && "Not handled");

        assert(!isa<IndirectBrInst>(T) && "Not handled");
        assert(!isa<InvokeInst>(T) && "Not handled");
        assert(!isa<ResumeInst>(T) && "Not handled");
        assert(!isa<ReturnInst>(T) && "Should not occur");
        assert(!isa<UnreachableInst>(T) && "Should not occur");

        if (auto *SI = dyn_cast<SwitchInst>(T)) {
            // Idea is to create a new basic block which will
            // only contain the guard call with false passed
            // as argument if the target block is not in the
            // chop.
            vector<uint32_t> ToPatch;
            for (uint32_t I = 0; I < SI->getNumSuccessors(); I++) {
                auto Tgt = SI->getSuccessor(I);
                if (!inChop(Tgt)) {
                    ToPatch.push_back(I);
                } else {
                    assert(VMap[Tgt] && "Switch target not found");
                    SI->setSuccessor(I, cast<BasicBlock>(VMap[Tgt]));
                }
            }

            if (ToPatch.size()) {
                // Create a guard block if it does not exist
                // already.
                if (!SwitchTgt) {
                    SwitchTgt = BasicBlock::Create(Context, "switch_guard",
                                                   StaticFunc, nullptr);
                    auto BoolType = IntegerType::getInt1Ty(Context);
                    Value *Arg = ConstantInt::get(BoolType, 0, false);
                    auto CI = CallInst::Create(GuardFunc, {Arg}, "", SwitchTgt);
                    CI->setDoesNotAccessMemory();
                    CI->setIsNoInline();
                    ReturnInst::Create(Context, SwitchTgt);
                }
                for (auto P : ToPatch) {
                    SI->setSuccessor(P, SwitchTgt);
                }
            }
        } else if (auto *BrInst = dyn_cast<BranchInst>(T)) {
            auto NS = T->getNumSuccessors();
            if (NS == 1) {
                // Unconditional branch target *must* exist in chop
                // since otherwith it would not be reachable from the
                // last block in the path.
                auto BJ = T->getSuccessor(0);
                assert(VMap[BJ] && "Value not found in map");
                T->setSuccessor(0, cast<BasicBlock>(VMap[BJ]));
            } else {
                SmallVector<BasicBlock *, 2> Targets;
                for (unsigned I = 0; I < NS; I++) {
                    auto BL = T->getSuccessor(I);
                    if (inChop(BL) &&
                        BackEdges.count(make_pair(*IT, BL)) == 0) {
                        assert(VMap[BL] && "Value not found in map");
                        Targets.push_back(cast<BasicBlock>(VMap[BL]));
                    }
                }

                assert(Targets.size() &&
                       "At least one target should be in the chop");

                // auto *BrInst = dyn_cast<BranchInst>(T);
                if (Targets.size() == 2) {
                    BrInst->setSuccessor(0, cast<BasicBlock>(Targets[0]));
                    BrInst->setSuccessor(1, cast<BasicBlock>(Targets[1]));
                } else {
                    GuardChecks++;
                    insertGuardCall(BrInst, BrInst->getParent());
                    T->eraseFromParent();
                    BranchInst::Create(cast<BasicBlock>(Targets[0]), NewBB);
                }
            }
        } else {
            assert(false && "Unknown TerminatorInst");
        }
    }

    auto handlePhis = [&VMap, &RevTopoChop, &LiveIn, &BackEdges](PHINode *Phi) {
        auto NV = Phi->getNumIncomingValues();
        vector<BasicBlock *> ToRemove;
        for (unsigned I = 0; I < NV; I++) {
            auto *Blk = Phi->getIncomingBlock(I);
            auto *Val = Phi->getIncomingValue(I);

            // Is this a backedge? Remove the incoming value
            // Is this predicated on a block outside the chop? Remove
            if (BackEdges.count(make_pair(Blk, Phi->getParent())) ||
                find(RevTopoChop.begin(), RevTopoChop.end(), Blk) ==
                    RevTopoChop.end()) {
                ToRemove.push_back(Blk);
                continue;
            }
            assert(VMap[Phi] &&
                   "New Phis should have been added by Instruction clone");
            auto *NewPhi = cast<PHINode>(VMap[Phi]);
            assert(VMap[Blk] && "Value not found in ValMap");
            NewPhi->setIncomingBlock(I, cast<BasicBlock>(VMap[Blk]));

            // Rewrite the value if it is available in the val map
            if (VMap.count(Val)) {
                NewPhi->setIncomingValue(I, VMap[Val]);
            }
        }
        for (auto R : ToRemove) {
            assert(VMap[Phi] &&
                   "New Phis should have been added by Instruction clone");
            auto *NewPhi = cast<PHINode>(VMap[Phi]);
            NewPhi->removeIncomingValue(R, false);
        }
    };

    // Patch the Phis of all the blocks in Topo order
    // apart from the first block (those become inputs)
    for (auto BB = next(RevTopoChop.rbegin()), BE = RevTopoChop.rend();
         BB != BE; BB++) {
        for (auto &Ins : **BB) {
            if (auto *Phi = dyn_cast<PHINode>(&Ins)) {
                handlePhis(Phi);
            }
        }
    }

    // Add store instructions to write live-outs to
    // the char array.
    const DataLayout *DL = StaticFunc->getParent()->getDataLayout();
    int32_t OutIndex = 0;
    auto Int32Ty = IntegerType::getInt32Ty(Context);
    ConstantInt *Zero = ConstantInt::get(Int32Ty, 0);
    for (auto &L : LiveOut) {
        // errs() << "Storing LO: " << *L << "\n";
        // FIXME : Sometimes L may not be in the block map since it could be a
        // self-loop (?)
        // if it is so then don't bother since all instructions are accounted
        // for.
        // EG: primal_net_simplex in mcf2000
        if (Value *LO = VMap[L]) {
            auto *Block = cast<Instruction>(LO)->getParent();
            ConstantInt *Index = ConstantInt::get(Int32Ty, OutIndex);
            Value *GEPIndices[] = {Zero, Index};
            auto *GEP = GetElementPtrInst::Create(LOA, GEPIndices, "idx",
                                                  Block->getTerminator());
            BitCastInst *BC =
                new BitCastInst(GEP, PointerType::get(LO->getType(), 0), "cast",
                                Block->getTerminator());
            new StoreInst(LO, BC, false, Block->getTerminator());
            OutIndex += DL->getTypeStoreSize(LO->getType());
        }
    }
}

static void getTopoChopHelper(
    BasicBlock *BB, DenseSet<BasicBlock *> &Chop, DenseSet<BasicBlock *> &Seen,
    SmallVector<BasicBlock *, 16> &Order,
    DenseSet<pair<const BasicBlock *, const BasicBlock *>> &BackEdges) {
    Seen.insert(BB);
    for (auto SB = succ_begin(BB), SE = succ_end(BB); SB != SE; SB++) {
        if (!Seen.count(*SB) && Chop.count(*SB)) {
            getTopoChopHelper(*SB, Chop, Seen, Order, BackEdges);
        }
    }
    Order.push_back(BB);
}

static SmallVector<BasicBlock *, 16>
getTopoChop(DenseSet<BasicBlock *> &Chop, BasicBlock *StartBB,
            DenseSet<pair<const BasicBlock *, const BasicBlock *>> &BackEdges) {
    SmallVector<BasicBlock *, 16> Order;
    DenseSet<BasicBlock *> Seen;
    getTopoChopHelper(StartBB, Chop, Seen, Order, BackEdges);
    return Order;
}

static inline bool checkIntrinsic(Function *F) {
    auto Name = F->getName();
    if (Name.startswith("llvm.dbg.") ||      // This will be stripped out
        Name.startswith("llvm.lifetime.") || // This will be stripped out
        Name.startswith("llvm.uadd.") ||     // Handled in the Verilog module
        Name.startswith("llvm.umul.") ||     // Handled in the Verilog module
        Name.startswith("llvm.bswap.") ||    // Handled in the Verilog module
        Name.startswith("llvm.fabs."))       // Handled in the Verilog module
        return false;
    else
        return true;
}

static bool verifyChop(const DenseSet<BasicBlock *> Chop) {
    for (auto &CB : Chop) {
        for (auto &I : *CB) {
            CallSite CS(&I);
            if (CS.isCall() || CS.isInvoke()) {
                if (!CS.getCalledFunction()) {
                    errs() << "Function Pointer\n";
                    return false;
                } else {
                    if (CS.getCalledFunction()->isDeclaration() &&
                        checkIntrinsic(CS.getCalledFunction())) {
                        DEBUG(errs() << "External Call : " << CS.getCalledFunction()->getName() << "\n");
                        return true;
                    }
                }
            }
        }
    }
    return true;
}

static DenseSet<pair<const BasicBlock *, const BasicBlock *>> 
getBackEdges(BasicBlock* StartBB) {
    SmallVector<std::pair<const BasicBlock *, const BasicBlock *>, 8>
        BackEdgesVec;
    FindFunctionBackedges(*StartBB->getParent(), BackEdgesVec);
    DenseSet<pair<const BasicBlock *, const BasicBlock *>> BackEdges;

    for (auto &BE : BackEdgesVec) {
        BackEdges.insert(BE);
    }
    return BackEdges;
}

static Function* 
generateStaticGraphFromPath(const Path &P,
                            map<string, BasicBlock *> &BlockMap,
                            PostDominatorTree *PDT,
                            Module* Mod) {

    auto *StartBB = BlockMap[P.Seq.front()];
    auto *LastBB = BlockMap[P.Seq.back()];
    auto DataLayoutStr = StartBB->getDataLayout();
    auto TargetTripleStr = StartBB->getParent()->getParent()->getTargetTriple();

    Mod->setDataLayout(DataLayoutStr);
    Mod->setTargetTriple(TargetTripleStr);

    DenseSet<Value *> LiveOut, Globals;
    SmallVector<Value *, 16> LiveIn;

    auto BackEdges = getBackEdges(StartBB);

    auto Chop = getChop(StartBB, LastBB, BackEdges);

    assert(verifyChop(Chop) && "Invalid Region!");

    auto RevTopoChop = getTopoChop(Chop, StartBB, BackEdges);

    auto handlePhis = [&LiveIn, &LiveOut, &Chop, &Globals, &StartBB, &BackEdges,
                       &PDT, &LastBB, &RevTopoChop](PHINode *Phi) -> int32_t {

        // Add uses of Phi before checking if it is LiveIn
        // What happens if this is promoted ot argument?
        for (auto UI = Phi->use_begin(), UE = Phi->use_end(); UI != UE; UI++) {
            if (auto UIns = dyn_cast<Instruction>(UI->getUser())) {
                auto Tgt = UIns->getParent();
                //if (!Chop.count(Tgt)) {
                if(find(RevTopoChop.begin(), RevTopoChop.end(), Tgt) == RevTopoChop.end()) {
                    // errs() << "Adding LO: " << *Ins << "\n";
                    if (!PDT->dominates(LastBB, UIns->getParent())) {
                        // Phi at the begining of path is a live-in,
                        // if it is used outside then don't care as
                        // the argument to the accelerated chop can be
                        // sent to the consumer outside the chop.
                        if (Phi->getParent() != StartBB) {
                            LiveOut.insert(Phi);
                        }
                    }
                } else {
                    // Handle loop-back Phi's
                    auto distance =
                        [&RevTopoChop](SmallVector<BasicBlock *, 16> &SV,
                                       BasicBlock *ToBB) -> uint32_t {
                            uint32_t I = 0;
                            for (auto &BB : RevTopoChop) {
                                if (BB == ToBB)
                                    return I;
                                I++;
                            }
                            assert(false && "Unreachable");
                        };
                    // Find the topo position of Tgt
                    // If it is topologically before the phi def, then it is a
                    // use
                    // across a backedge which we must add as live-out
                    auto PosTgt = distance(RevTopoChop, Tgt);
                    auto PosPhi = distance(RevTopoChop, Phi->getParent());
                    if (PosTgt > PosPhi) {
                        LiveOut.insert(Phi);
                    }
                }
            }
        } // End of handling for LiveOut

        if (Phi->getParent() == StartBB) {
            LiveIn.push_back(Phi);
            return 1;
        }

        uint32_t Num = 0;
        for (uint32_t I = 0; I < Phi->getNumIncomingValues(); I++) {
            auto *Blk = Phi->getIncomingBlock(I);
            auto *Val = Phi->getIncomingValue(I);
            //if (Chop.count(Blk)) {
            if(find(RevTopoChop.begin(), RevTopoChop.end(), Blk) != RevTopoChop.end()) {
                if (auto *VI = dyn_cast<Instruction>(Val)) {
                    //if (Chop.count(VI->getParent()) == 0) {
                    if(find(RevTopoChop.begin(), RevTopoChop.end(), VI->getParent()) == RevTopoChop.end()) {
                        LiveIn.push_back(Val);
                        Num += 1;
                    }
                } else if (auto *AI = dyn_cast<Argument>(Val)) {
                    LiveIn.push_back(AI);
                    Num += 1;
                } else if (auto *GV = dyn_cast<GlobalVariable>(Val)) {
                    errs() << *GV << "\n";
                    Globals.insert(GV);
                }
            }
        }
        return Num;
    };

    // Collect the live-ins and live-outs for the Chop
    uint32_t PhiLiveIn = 0;
    //for (auto &BB : Chop) {
    for (auto &BB : RevTopoChop) {
        for (auto &I : *BB) {
            if (auto Phi = dyn_cast<PHINode>(&I)) {
                PhiLiveIn += handlePhis(Phi);
                continue;
            }
            liveInHelperStatic(Chop, LiveIn, Globals, &I);
            // Live-Outs
            if (auto Ins = dyn_cast<Instruction>(&I)) {
                for (auto UI = Ins->use_begin(), UE = Ins->use_end(); UI != UE;
                     UI++) {
                    if (auto UIns = dyn_cast<Instruction>(UI->getUser())) {
                        //if (!Chop.count(UIns->getParent())) {
                        if(find(RevTopoChop.begin(), RevTopoChop.end(), UIns->getParent()) == RevTopoChop.end()) {
                            // errs() << "Adding LO: " << *Ins << "\n";
                            // Need to reason about this check some more.
                            if (!PDT->dominates(LastBB, UIns->getParent()) &&
                                !isa<GetElementPtrInst>(Ins)) {
                                LiveOut.insert(Ins);
                            }
                        }
                    }
                }
            }
        }
    }

    // Add the live-out which is the return value from the
    // chop if it exists since we return void.

    auto *LastT = LastBB->getTerminator();
    if (auto *RT = dyn_cast<ReturnInst>(LastT)) {
        if (auto *Val = RT->getReturnValue()) {
            LiveOut.insert(Val);
        }
    }

    auto VoidTy = Type::getVoidTy(Mod->getContext());

    std::vector<Type *> ParamTy;
    // Add the types of the input values
    // to the function's argument list
    for (auto Val : LiveIn)
        ParamTy.push_back(Val->getType());

    FunctionType *StFuncType = FunctionType::get(VoidTy, ParamTy, false);

    // Create the trace function
    Function *StaticFunc = Function::Create(
        StFuncType, GlobalValue::ExternalLinkage, "__static_func", Mod);

    // Create an external function which is used to
    // model all guard checks.
    auto Int1Ty = IntegerType::getInt1Ty(Mod->getContext());
    FunctionType *GuFuncType = FunctionType::get(VoidTy, Int1Ty, false);

    // Create the guard function
    Function *GuardFunc = Function::Create(
        GuFuncType, GlobalValue::ExternalLinkage, "__guard_func", Mod);

    // // Create the Output Array as a global variable
    uint32_t LiveOutSize = 0;
    const DataLayout *DL = Mod->getDataLayout();
    for_each(LiveOut.begin(), LiveOut.end(),
             [&LiveOutSize, &DL](const Value *Val) {
                 LiveOutSize += DL->getTypeStoreSize(Val->getType());
             });
    ArrayType *CharArrTy =
        ArrayType::get(IntegerType::get(Mod->getContext(), 8), LiveOutSize);
    auto *Initializer = ConstantAggregateZero::get(CharArrTy);
    GlobalVariable *LOA =
        new GlobalVariable(*Mod, CharArrTy, false, GlobalValue::CommonLinkage,
                           Initializer, "__live_outs");

    LOA->setAlignment(8);

    staticHelper(StaticFunc, GuardFunc, LOA, LiveIn, LiveOut, Globals,
                 RevTopoChop, BackEdges, Mod->getContext());

    StripDebugInfo(*Mod);

    DEBUG(errs() << "Verifying " << P.Id << "\n");
    // Dumbass verifyModule function returns false if no
    // errors are found. Ref "llvm/IR/Verifier.h":46
    assert(!verifyModule(*Mod, &errs()) && "Module verification failed!");
    return StaticFunc;
}

static void 
optimizeModule(Module* Mod) {
    PassManagerBuilder PMB;
    PMB.OptLevel = 3;
    PMB.SLPVectorize = false;
    PMB.BBVectorize = false;
    PassManager PM;
    PMB.populateModulePassManager(PM);
    PM.run(*Mod);
}

static void
writeModule(Module* Mod, string Name) {
    error_code EC;
    raw_fd_ostream File(Name, EC, sys::fs::OpenFlags::F_RW);
    Mod->print(File, nullptr);
    File.close();
}

void MicroWorkloadExtract::makeSeqGraph(Function &F) {
    PostDomTree = &getAnalysis<PostDominatorTree>(F);
    AA = &getAnalysis<AliasAnalysis>();

    map<string, BasicBlock *> BlockMap;
    for (auto &BB : F)
        BlockMap[BB.getName().str()] = &BB;

    for (auto &P : Sequences) {
        Module *Mod = new Module(P.Id + string("-static"), getGlobalContext());
        auto *ExF = generateStaticGraphFromPath(P, BlockMap, PostDomTree, Mod);
        optimizeModule(Mod);
        writeModule(Mod, (P.Id) + string(".static.ll"));
        delete Mod;
    }

    // Generate block set
    // a. Chop
    // b. Unify
    // c. Trace
    // Extract function
    // Reintegrate function
}

bool MicroWorkloadExtract::runOnModule(Module &M) {
    for (auto &F : M)
        //if (F.getName() == TargetFunction)
        if (isTargetFunction(F, FunctionList))
            makeSeqGraph(F);

    return false;
}

char MicroWorkloadExtract::ID = 0;
