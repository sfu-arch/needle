#include "MicroWorkloadExtract.h"
#include <boost/algorithm/string.hpp>
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include "llvm/IR/Verifier.h"
#include "llvm/PassManager.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
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
#include "llvm/IR/DerivedTypes.h"
#include <cxxabi.h>
#include "llvm/Linker/Linker.h"
#include "Common.h"

#define DEBUG_TYPE "mw"

using namespace llvm;
using namespace mwe;
using namespace std;

extern cl::list<std::string> FunctionList;
extern bool isTargetFunction(const Function &f,
                             const cl::list<std::string> &FunctionList);

void MicroWorkloadExtract::readSequences() {
    ifstream SeqFile(SeqFilePath.c_str(), ios::in);
    assert(SeqFile.is_open() && "Could not open file");
    string Line;
    // TODO: We can do more than one in case they are not overlapping
    // I will add the checks later.
    assert(NumSeq == 1 && "Can't do more than 1 since original"
            "bitcode is modified");
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

        move(Tokens.begin() + 4, Tokens.end() - 1, back_inserter(P.Seq));
        Sequences.push_back(P);
        errs() << *P.Seq.begin() << " " << *P.Seq.rbegin() << "\n";
        Count++;
        if (Count == NumSeq)
            break;
    }
    SeqFile.close();
}

bool MicroWorkloadExtract::doInitialization(Module &M) {
    readSequences();
    return false;
}

bool MicroWorkloadExtract::doFinalization(Module &M) { return false; }

static bool isBlockInPath(const string &S, const Path &P) {
    return find(P.Seq.begin(), P.Seq.end(), S) != P.Seq.end();
}

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

static void liveInHelperStatic(SmallVector<BasicBlock *, 16> &RevTopoChop,
                               SetVector<Value*> &LiveIn,
                               SetVector<Value*> &Globals, Value *Val) {
    if (auto Ins = dyn_cast<Instruction>(Val)) {
        for (auto OI = Ins->op_begin(), EI = Ins->op_end(); OI != EI; OI++) {
            if (auto OIns = dyn_cast<Instruction>(OI)) {
                // if (!Chop.count(OIns->getParent())) {
                if (find(RevTopoChop.begin(), RevTopoChop.end(),
                         OIns->getParent()) == RevTopoChop.end()) {
                    LiveIn.insert(OIns);
                }
            } else
                liveInHelperStatic(RevTopoChop, LiveIn, Globals, *OI);
        }
    } else if (auto CE = dyn_cast<ConstantExpr>(Val)) {
        for (auto OI = CE->op_begin(), EI = CE->op_end(); OI != EI; OI++) {
            assert(!isa<Instruction>(OI) &&
                   "Don't expect operand of ConstExpr to be an Instruction");
            liveInHelperStatic(RevTopoChop, LiveIn, Globals, *OI);
        }
    } else if (auto Arg = dyn_cast<Argument>(Val))
        LiveIn.insert(Arg);
    else if (auto GV = dyn_cast<GlobalVariable>(Val))
        Globals.insert(GV);

    // Constants should just fall through and remain
    // in the trace.
}

void 
MicroWorkloadExtract::extractHelper(Function *StaticFunc, Function *GuardFunc,
                  SetVector<Value *> &LiveIn, SetVector<Value *> &LiveOut,
                  SetVector<Value *> &Globals,
                  SmallVector<BasicBlock *, 16> &RevTopoChop,
                  LLVMContext &Context) {

    ValueToValueMapTy VMap;
    auto BackEdges = common::getBackEdges(RevTopoChop.back());

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

    function<void(Value &)> handleOperands;
    handleOperands = [&VMap, &handleOperands, &StaticFunc](Value &V) {
        User &I = *cast<User>(&V);
        for (auto OI = I.op_begin(), E = I.op_end(); OI != E; ++OI) {
            if (auto CE = dyn_cast<ConstantExpr>(*OI)) {
                handleOperands(*CE);
            }
            if (auto *GV = dyn_cast<GlobalVariable>(*OI)) {
                // Since we may have already patched the global
                // don't try to patch it again.
                if (VMap.count(GV) == 0)
                    continue;
                // Check if we came from a ConstantExpr
                if (auto CE = dyn_cast<ConstantExpr>(&V)) {
                    int32_t OpIdx = -1;
                    while (I.getOperand(++OpIdx) != GV)
                        ;
                    auto NCE = CE->getWithOperandReplaced(
                        OpIdx, cast<Constant>(VMap[GV]));
                    vector<User *> Users(CE->user_begin(), CE->user_end());
                    for (auto U = Users.begin(), UE = Users.end(); U != UE;
                         ++U) {
                        // All users of ConstExpr should be instructions
                        auto Ins = dyn_cast<Instruction>(*U);
                        if (Ins->getParent()->getParent() == StaticFunc) {
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

    // Add return true for last block
    auto *BB = cast<BasicBlock>(VMap[RevTopoChop.front()]);
    BB->getTerminator()->eraseFromParent();
    ReturnInst::Create(Context, ConstantInt::getTrue(Context), BB);

    // Patch branches
    auto insertGuardCall =
        [&GuardFunc, &Context](BranchInst *CBR, bool FreqCondition) {
            auto *Blk = CBR->getParent();
            Value *Arg = CBR->getCondition();
            Value *Dom = FreqCondition ? ConstantInt::getTrue(Context) 
                                : ConstantInt::getFalse(Context);
            vector<Value*> Params = {Arg, Dom};
            auto CI = CallInst::Create(GuardFunc, Params, "", Blk);
            // Add a ReadNone+NoInline attribute to the CallSite, which
            // will hopefully help the optimizer.
            CI->setDoesNotAccessMemory();
            CI->setIsNoInline();
        };

    for (auto IT = next(RevTopoChop.begin()), IE = RevTopoChop.end(); IT != IE;
         IT++) {
        auto *NewBB = cast<BasicBlock>(VMap[*IT]);
        auto T = NewBB->getTerminator();

        assert(!isa<IndirectBrInst>(T) && "Not handled");
        assert(!isa<InvokeInst>(T) && "Not handled");
        assert(!isa<ResumeInst>(T) && "Not handled");
        assert(!isa<ReturnInst>(T) && "Should not occur");
        assert(!isa<UnreachableInst>(T) && "Should not occur");

        if (auto *SI = dyn_cast<SwitchInst>(T)) {
            assert(false && "Switch instruction not handled, "
                    "use LowerSwitchPass to convert switch to if-else.");
        } else if (auto *BrInst = dyn_cast<BranchInst>(T)) {
            if(extractAsChop) {
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
                        if(inChop(T->getSuccessor(0))) {
                            insertGuardCall(BrInst, true);
                        } else  {
                            insertGuardCall(BrInst, false);
                        }
                        T->eraseFromParent();
                        BranchInst::Create(cast<BasicBlock>(Targets[0]), NewBB);
                    }
                }
            } else {
                // Trace will replace the terminator inst with a direct branch to
                // the successor, the DCE pass will remove the comparison and the 
                // simplification with merge the basic blocks later.
                if(T->getNumSuccessors() > 0) {
                    auto *SuccBB = *prev(IT);
                    vector<BasicBlock*> Succs(succ_begin(*IT), succ_end(*IT));
                    assert(find(Succs.begin(), Succs.end(), SuccBB) != Succs.end() && 
                            "Could not find successor!");
                    assert(VMap[SuccBB] && "Successor not found in VMap");
                    if(T->getNumSuccessors() == 2) {
                        if(T->getSuccessor(0) == SuccBB)
                            insertGuardCall(BrInst, true);
                        else 
                            insertGuardCall(BrInst, false);
                    }
                    T->eraseFromParent(); 
                    BranchInst::Create(cast<BasicBlock>(VMap[SuccBB]), NewBB);
                }
            }
        } else {
            assert(false && "Unknown TerminatorInst");
        }
    }
    
    // FIXME : At a higher level, induction variable Phi's should become 
    // live-in which is then supplied by the wrapper in a loop.
    auto handlePhis = [&VMap, &RevTopoChop, &BackEdges](PHINode *Phi, bool extractAsChop) {
        auto NV = Phi->getNumIncomingValues();
        vector<BasicBlock *> ToRemove;
        for (unsigned I = 0; I < NV; I++) {
            auto *Blk = Phi->getIncomingBlock(I);
            auto *Val = Phi->getIncomingValue(I);

            if(!extractAsChop && 
                    *next(find(RevTopoChop.begin(), RevTopoChop.end(), Phi->getParent())) != Blk) {
                ToRemove.push_back(Blk);
                continue;
            }

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
                handlePhis(Phi, extractAsChop);
            }
        }
    }

    // Get the struct pointer from the argument list,
    // assume that output struct is always last arg
    auto StructPtr = --StaticFunc->arg_end();

    // Store the live-outs to the output struct
    int32_t OutIndex = 0;
    Value *Idx[2];
    Idx[0] = Constant::getNullValue(Type::getInt32Ty(Context));
    for (auto &L : LiveOut) {
        Value *LO = VMap[L];
        assert(LO && "Live Out not remapped");
        auto *Block = cast<Instruction>(LO)->getParent();
        Idx[1] = ConstantInt::get(Type::getInt32Ty(Context), OutIndex);
        GetElementPtrInst *StructGEP = GetElementPtrInst::Create(
            StructPtr, Idx, "", Block->getTerminator());
        auto *SI = new StoreInst(LO, StructGEP, Block->getTerminator());
        MDNode* N = MDNode::get(Context, MDString::get(Context, "true"));
        SI->setMetadata("LO", N);
        OutIndex++;
    }

    // Add store instructions to write live-outs to
    // the char array.
    // const DataLayout *DL = StaticFunc->getParent()->getDataLayout();
    // OutIndex = 0;
    // auto Int32Ty = IntegerType::getInt32Ty(Context);
    // ConstantInt *Zero = ConstantInt::get(Int32Ty, 0);
    // for (auto &L : LiveOut) {
    //     // errs() << "Storing LO: " << *L << "\n";
    //     // FIXME : Sometimes L may not be in the block map since it could be
    //     a
    //     // self-loop (?)
    //     // if it is so then don't bother since all instructions are accounted
    //     // for.
    //     // EG: primal_net_simplex in mcf2000
    //     if (Value *LO = VMap[L]) {
    //         auto *Block = cast<Instruction>(LO)->getParent();
    //         ConstantInt *Index = ConstantInt::get(Int32Ty, OutIndex);
    //         Value *GEPIndices[] = {Zero, Index};
    //         auto *GEP = GetElementPtrInst::Create(LOA, GEPIndices, "idx",
    //                                               Block->getTerminator());
    //         BitCastInst *BC =
    //             new BitCastInst(GEP, PointerType::get(LO->getType(), 0),
    //             "cast",
    //                             Block->getTerminator());
    //         new StoreInst(LO, BC, false, Block->getTerminator());
    //         OutIndex += DL->getTypeStoreSize(LO->getType());
    //     }
    // }
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

SmallVector<BasicBlock *, 16>
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

static bool verifyChop(const SmallVector<BasicBlock *, 16> Chop) {
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
                        DEBUG(errs() << "External Call : "
                                     << CS.getCalledFunction()->getName()
                                     << "\n");
                        return true;
                    }
                }
            }
        }
    }
    return true;
}

static StructType*
getLiveOutStructType(SetVector<Value*> &LiveOut, Module* Mod) {
    SmallVector<Type *, 16> LiveOutTypes(LiveOut.size());
    transform(LiveOut.begin(), LiveOut.end(), LiveOutTypes.begin(),
              [](const Value *V) -> Type *{ return V->getType(); });
    // Create a packed struct return type
    return StructType::get(Mod->getContext(), LiveOutTypes, true);
}

Function *
MicroWorkloadExtract::extract(PostDominatorTree *PDT, Module *Mod,
                                   SmallVector<BasicBlock *, 16> &RevTopoChop,
                                   SetVector<Value*> &LiveIn,
                                   SetVector<Value*> &LiveOut) {

    auto *StartBB = RevTopoChop.back();
    auto *LastBB = RevTopoChop.front();

    assert(verifyChop(RevTopoChop) && "Invalid Region!");

    SetVector<Value *> Globals;

    auto handlePhis =
        [&LiveIn, &LiveOut, &Globals, &StartBB, &PDT, &LastBB, &RevTopoChop](
            PHINode *Phi) -> int32_t {

            // Add uses of Phi before checking if it is LiveIn
            // What happens if this is promoted ot argument?
            for (auto UI = Phi->use_begin(), UE = Phi->use_end(); UI != UE;
                 UI++) {
                if (auto UIns = dyn_cast<Instruction>(UI->getUser())) {
                    auto Tgt = UIns->getParent();
                    // if (!Chop.count(Tgt)) {
                    if (find(RevTopoChop.begin(), RevTopoChop.end(), Tgt) ==
                        RevTopoChop.end()) {
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
                        // If it is topologically before the phi def, then it is
                        // a
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
                LiveIn.insert(Phi);
                return 1;
            }

            uint32_t Num = 0;
            for (uint32_t I = 0; I < Phi->getNumIncomingValues(); I++) {
                auto *Blk = Phi->getIncomingBlock(I);
                auto *Val = Phi->getIncomingValue(I);
                // if (Chop.count(Blk)) {
                if (find(RevTopoChop.begin(), RevTopoChop.end(), Blk) !=
                    RevTopoChop.end()) {
                    if (auto *VI = dyn_cast<Instruction>(Val)) {
                        // if (Chop.count(VI->getParent()) == 0) {
                        if (find(RevTopoChop.begin(), RevTopoChop.end(),
                                 VI->getParent()) == RevTopoChop.end()) {
                            LiveIn.insert(Val);
                            Num += 1;
                        }
                    } else if (auto *AI = dyn_cast<Argument>(Val)) {
                        LiveIn.insert(AI);
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
    // for (auto &BB : Chop) {
    // The order of iteration of blocks in this loop,
    // does not matter.
    for (auto &BB : RevTopoChop) {
        for (auto &I : *BB) {
            if (auto Phi = dyn_cast<PHINode>(&I)) {
                PhiLiveIn += handlePhis(Phi);
                continue;
            }
            liveInHelperStatic(RevTopoChop, LiveIn, Globals, &I);
            // Live-Outs
            if (auto Ins = dyn_cast<Instruction>(&I)) {
                for (auto UI = Ins->use_begin(), UE = Ins->use_end(); UI != UE;
                     UI++) {
                    if (auto UIns = dyn_cast<Instruction>(UI->getUser())) {
                        // if (!Chop.count(UIns->getParent())) {
                        if (find(RevTopoChop.begin(), RevTopoChop.end(),
                                 UIns->getParent()) == RevTopoChop.end()) {
                            // errs() << "Adding LO: " << *Ins << "\n";
                            // Need to reason about this check some more.
                            //if (!PDT->dominates(LastBB, UIns->getParent()) &&
                                //!isa<GetElementPtrInst>(Ins)) {
                                //LiveOut.insert(Ins);
                            //}
                            if (!PDT->dominates(LastBB, UIns->getParent())){
                                //if(!isa<GetElementPtrInst>(Ins)) {
                                    LiveOut.insert(Ins);
                                //}
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

    auto DataLayoutStr = StartBB->getDataLayout();
    auto TargetTripleStr = StartBB->getParent()->getParent()->getTargetTriple();
    Mod->setDataLayout(DataLayoutStr);
    Mod->setTargetTriple(TargetTripleStr);

    //SmallVector<Type *, 16> LiveOutTypes(LiveOut.size());
    //transform(LiveOut.begin(), LiveOut.end(), LiveOutTypes.begin(),
              //[](const Value *V) -> Type *{ return V->getType(); });
    // Create a packed struct return type
    auto *StructTy = getLiveOutStructType(LiveOut, Mod);
    auto *StructPtrTy = PointerType::getUnqual(StructTy);

    // errs() << *StructPtrTy << "\n";

    // Bool return type for extracted function
    auto VoidTy = Type::getVoidTy(Mod->getContext());
    auto Int1Ty = IntegerType::getInt1Ty(Mod->getContext());

    std::vector<Type *> ParamTy;
    // Add the types of the input values
    // to the function's argument list
    for (auto Val : LiveIn)
        ParamTy.push_back(Val->getType());

    ParamTy.push_back(StructPtrTy);

    FunctionType *StFuncType = FunctionType::get(Int1Ty, ParamTy, false);

    // Create the trace function
    Function *StaticFunc = Function::Create(
        StFuncType, GlobalValue::ExternalLinkage, "__offload_func", Mod);

    // Create an external function which is used to
    // model all guard checks. First arg is the condition, second is whether 
    // the condition is dominant as true or as false. This 
    // guard func is later replaced by a branch and return statement.
    // we use this as placeholder to create a superblock and enable
    // optimizations.
    ParamTy.clear(); ParamTy = {Int1Ty, Int1Ty};
    FunctionType *GuFuncType = FunctionType::get(VoidTy, ParamTy, false);

    // Create the guard function
    Function *GuardFunc = Function::Create(
        GuFuncType, GlobalValue::ExternalLinkage, "__guard_func", Mod);

    // // Create the Output Array as a global variable
    // uint32_t LiveOutSize = 0;
    // const DataLayout *DL = Mod->getDataLayout();
    // for_each(LiveOut.begin(), LiveOut.end(),
    //          [&LiveOutSize, &DL](const Value *Val) {
    //              LiveOutSize += DL->getTypeStoreSize(Val->getType());
    //          });
    // ArrayType *CharArrTy =
    //     ArrayType::get(IntegerType::get(Mod->getContext(), 8), LiveOutSize);
    // auto *Initializer = ConstantAggregateZero::get(CharArrTy);
    // GlobalVariable *LOA =
    //     new GlobalVariable(*Mod, CharArrTy, false,
    //     GlobalValue::CommonLinkage,
    //                        Initializer, "__live_outs");

    // LOA->setAlignment(8);

    // staticHelper(StaticFunc, GuardFunc, LOA, LiveIn, LiveOut, Globals,
    extractHelper(StaticFunc, GuardFunc, LiveIn, LiveOut, Globals, RevTopoChop,
                 Mod->getContext());

    StripDebugInfo(*Mod);

    // Dumbass verifyModule function returns false if no
    // errors are found. Ref "llvm/IR/Verifier.h":46
    assert(!verifyModule(*Mod, &errs()) && "Module verification failed!");
    return StaticFunc;
}


static void writeModule(Module *Mod, string Name) {
    error_code EC;
    raw_fd_ostream File(Name, EC, sys::fs::OpenFlags::F_RW);
    Mod->print(File, nullptr);
    File.close();
}

static SmallVector<BasicBlock *, 16>
getChopBlocks(Path &P, map<string, BasicBlock *> &BlockMap) {
    auto *StartBB = BlockMap[P.Seq.front()];
    auto *LastBB = BlockMap[P.Seq.back()];
    auto BackEdges = common::getBackEdges(StartBB);
    auto Chop = getChop(StartBB, LastBB, BackEdges);
    auto RevTopoChop = getTopoChop(Chop, StartBB, BackEdges);
    assert(StartBB == RevTopoChop.back() && LastBB == RevTopoChop.front() &&
           "Sanity Check");
    return RevTopoChop;
}

static SmallVector<BasicBlock *, 16>
getTraceBlocks(Path &P, map<string, BasicBlock *> &BlockMap) {
    SmallVector<BasicBlock *, 16> RPath;
    for (auto RB = P.Seq.rbegin(), RE = P.Seq.rend(); RB != RE; RB++) {
        assert(BlockMap[*RB] && "Path does not exist");
        RPath.push_back(BlockMap[*RB]);
    }
    return RPath;
}


static void
instrumentFunction(Function& F, SmallVector<BasicBlock*, 16>& Blocks, 
                    FunctionType* OffloadTy, FunctionType* UndoTy,
                    SetVector<Value*> &LiveIn,
                    SetVector<Value*> &LiveOut,
                    PostDominatorTree* PDT){
    BasicBlock* StartBB = Blocks.back(), *LastBB = Blocks.front();
    
    auto &Ctx = F.getContext();
    auto *Mod = F.getParent();
    
    auto *Offload = Mod->getFunction("__offload_func");
    
    auto *SSplit = StartBB->splitBasicBlock(StartBB->getFirstInsertionPt());
    SSplit->setName(StartBB->getName()+".split");
    auto *LSplit = LastBB->splitBasicBlock(LastBB->getTerminator());
    LSplit->setName(LastBB->getName()+".split");
    
    auto *Success = BasicBlock::Create(Ctx, "offload.true", &F);
    auto *Fail = BasicBlock::Create(Ctx, "offload.false", &F);

    StartBB->getTerminator()->eraseFromParent();
    // Get all the live-ins
    // Allocate a struct to get the live-outs filled in
    // Call offload function and check the return
    auto *StructTy = getLiveOutStructType(LiveOut, Mod);
    auto *LOS = new AllocaInst(StructTy, nullptr, "", StartBB);
    auto *Int64Ty = Type::getInt64Ty(Mod->getContext());
    ConstantInt *Zero = ConstantInt::get(Int64Ty, 0);
    auto *StPtr = GetElementPtrInst::CreateInBounds(LOS, {Zero}, "", StartBB);

    vector<Value*> Params;
    for(auto &V : LiveIn) Params.push_back(V);
    Params.push_back(StPtr);

    auto *CI = CallInst::Create(Offload, Params, "", StartBB);
    BranchInst::Create(Success, Fail, CI, StartBB);
    
    // Success -- Unpack struct
    //CallInst::Create(Mod->getFunction("__success"), {}, "", Success);
    for(uint32_t Idx = 0; Idx < LiveOut.size(); Idx++) {
        auto *Val = LiveOut[Idx];
        Value *StIdx = ConstantInt::get(Int64Ty, Idx, false);
        auto *ValPtr = GetElementPtrInst::CreateInBounds(StPtr, {StIdx}, "st_gep", Success);
        auto *Load = new LoadInst(ValPtr, "", Success);
        vector<User*> Users(Val->user_begin(), Val->user_end());
        for(auto &U : Users) {
            auto *UseBB = dyn_cast<Instruction>(U)->getParent();
            if(PDT->dominates(UseBB, LastBB)) {
                U->replaceUsesOfWith(Val, Load);                    
            }
        }
    }
    BranchInst::Create(LSplit, Success);

    auto *Undo = Mod->getFunction("__undo_mem");
    auto *ULog = Mod->getNamedGlobal("__undo_log");
    auto *NumStore = Mod->getNamedGlobal("__undo_num_stores");
   
    vector<Value*> Idx = {Zero, Zero};
    auto *UGEP = GetElementPtrInst::CreateInBounds(ULog, Idx, "", Fail);
    auto *UNS = GetElementPtrInst::CreateInBounds(NumStore, {Zero}, "", Fail);
    auto *NSLoad = new LoadInst(UNS, "", Fail);
    // Fail -- Undo memory
    vector<Value*> Args = {UGEP, NSLoad};
    CallInst::Create(Undo, Args, "", Fail);
    //CallInst::Create(Mod->getFunction("__fail"), {}, "", Fail);
    BranchInst::Create(SSplit, Fail);
}

static void
runHelperPasses(Function* Offload, Function *Undo, Module* Generated) {
    PassManager PM;
    PM.add(createBasicAliasAnalysisPass());
    PM.add(createTypeBasedAliasAnalysisPass());
    PM.add(new MicroWorkloadHelper(Offload, Undo));
    PM.run(*Generated);
}

void 
MicroWorkloadExtract::process(Function &F) {
    PostDomTree = &getAnalysis<PostDominatorTree>(F);

    map<string, BasicBlock *> BlockMap;
    for (auto &BB : F)
        BlockMap[BB.getName().str()] = &BB;

    for (auto &P : Sequences) {

        Module *Mod = new Module(P.Id, getGlobalContext());
        SmallVector<BasicBlock*, 16> Blocks = extractAsChop ? 
                    getChopBlocks(P, BlockMap) : getTraceBlocks(P, BlockMap);

        // Extract the blocks and create a new function
        SetVector<Value*> LiveOut, LiveIn;
        Function *Offload = extract(PostDomTree, Mod, Blocks, LiveIn, LiveOut);

        // Creating a definition of the Undo function here and 
        // then creating the body inside the pass causes LLVM to 
        // crash thus nullptr is passed.
        runHelperPasses(Offload, nullptr, Mod);
        writeModule(Mod, (P.Id) + string(".ll"));

        // Link modules now since the required globals 
        // have been created.
        Linker::LinkModules(Mod, UndoMod);
        Linker::LinkModules(F.getParent(), Mod);

        instrumentFunction(F, Blocks, Offload->getFunctionType(), nullptr,
                            LiveIn, LiveOut, PostDomTree);
        //StripDebugInfo(*F.getParent());
        writeModule(F.getParent(), string("app.inst.ll"));
        // Replace with LLVMWriteBitcodeToFile(const Module *M, char* Path);
        delete Mod;
    }
}

bool MicroWorkloadExtract::runOnModule(Module &M) {

    for (auto &F : M)
        if (isTargetFunction(F, FunctionList))
            process(F);

    return false;
}

char MicroWorkloadExtract::ID = 0;
