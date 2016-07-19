#define DEBUG_TYPE "pasha_mwe"
#include "MicroWorkloadExtract.h"
#include "Common.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/TypeBasedAliasAnalysis.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"
#include <boost/algorithm/string.hpp>
#include <cxxabi.h>

#include <algorithm>
#include <deque>

using namespace llvm;
using namespace mwe;
using namespace std;

extern cl::list<std::string> FunctionList;
extern bool isTargetFunction(const Function &f,
                             const cl::list<std::string> &FunctionList);

extern cl::opt<bool> SimulateDFG;

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

static void liveInHelper(SmallVector<BasicBlock *, 16> &RevTopoChop,
                         SetVector<Value *> &LiveIn,
                         SetVector<Value *> &Globals, Value *Val) {
    if (auto Ins = dyn_cast<Instruction>(Val)) {
        for (auto OI = Ins->op_begin(), EI = Ins->op_end(); OI != EI; OI++) {
            if (auto OIns = dyn_cast<Instruction>(OI)) {
                if (find(RevTopoChop.begin(), RevTopoChop.end(),
                         OIns->getParent()) == RevTopoChop.end()) {
                    LiveIn.insert(OIns);
                }
            } else
                liveInHelper(RevTopoChop, LiveIn, Globals, *OI);
        }
    } else if (auto CE = dyn_cast<ConstantExpr>(Val)) {
        for (auto OI = CE->op_begin(), EI = CE->op_end(); OI != EI; OI++) {
            assert(!isa<Instruction>(OI) &&
                   "Don't expect operand of ConstExpr to be an Instruction");
            liveInHelper(RevTopoChop, LiveIn, Globals, *OI);
        }
    } else if (auto Arg = dyn_cast<Argument>(Val))
        LiveIn.insert(Arg);
    else if (auto GV = dyn_cast<GlobalVariable>(Val))
        Globals.insert(GV);

    // Constants should just fall through and remain
    // in the trace.
}

void MicroWorkloadExtract::extractHelper(
    Function *StaticFunc, Function *GuardFunc, SetVector<Value *> &LiveIn,
    SetVector<Value *> &LiveOut, SetVector<Value *> &Globals,
    SmallVector<BasicBlock *, 16> &RevTopoChop, LLVMContext &Context) {

    ValueToValueMapTy VMap;
    auto BackEdges = common::getBackEdges(RevTopoChop.back());

    auto handleCallSites = [&VMap, &StaticFunc](CallSite &OrigCS,
                                                CallSite &StaticCS) {
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
        // Just set the linkage for the original global variable in
        // case the it was private or something.
        OldGV->setLinkage(GlobalValue::ExternalLinkage);
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

    auto rewriteUses = [&VMap, &RevTopoChop, &StaticFunc](Value *Val,
                                                          Value *RewriteVal) {
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
        Value *RewriteVal = &*AI++;
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


    auto handleCExpr = [] (ConstantExpr *CE, Value *Old, Value *New) -> ConstantExpr* {
        int32_t OpIdx = -1;
        while (CE->getOperand(++OpIdx) != Old);
        auto NCE = CE->getWithOperandReplaced(OpIdx, cast<Constant>(New));
        return cast<ConstantExpr>(NCE);
    };
    
    auto replaceIfOutlined = [&StaticFunc](Instruction* I, 
            Value* Old, Value* New) {
        if (I->getParent()->getParent() == StaticFunc) {
            I->replaceUsesOfWith(Old, New);
        }
    };
   
    //vector<ConstantExpr*> UpdateCExpr;
    deque<ConstantExpr*> UpdateCExpr;  
    
    for(auto &GV : Globals) {
        // Get the user of the global
        vector<User *> Users(GV->user_begin(), GV->user_end());
        for (auto &U : Users ) {
            if(auto I = dyn_cast<Instruction>(U)) {
                replaceIfOutlined(I, GV, VMap[GV]);
            } else if(auto CE = dyn_cast<ConstantExpr>(U)) {
                auto NCE = handleCExpr(CE, GV, VMap[GV]);
                VMap[CE] = NCE;
                UpdateCExpr.push_back(CE);
            } else {
                assert(false && "Unexpected Global User");
            }
        }
    }


    // Update the uses of the constant expression which now have
    // copies if their use exists in the outlined region.

    vector<ConstantExpr*> AllCExpr(UpdateCExpr.begin(), UpdateCExpr.end());
    // for(auto &CE : UpdateCExpr) {
    //     vector<ConstantExpr*> More;
    //     for (auto U = CE->user_begin(), 
    //             UE = CE->user_end(); U != UE; U++) {
    //         if(auto UCE = dyn_cast<ConstantExpr>(*U)) {
    //             assert(VMap.count(CE) && "Mapping for ConstantExpr");
    //             auto NCE = handleCExpr(UCE, CE, VMap[CE]);
    //             VMap[UCE] = NCE;
    //             More.push_back(UCE);
    //         }
    //     }
    //     AllCExpr.insert(AllCExpr.end(), More.begin(), More.end());
    //     UpdateCExpr.clear();
    //     copy(More.begin(), More.end(), back_inserter(UpdateCExpr));
    // }

    while(!UpdateCExpr.empty()) {
        auto CE = UpdateCExpr.pop_front();
         for (auto U = CE->user_begin(), 
                UE = CE->user_end(); U != UE; U++) {
            if(auto UCE = dyn_cast<ConstantExpr>(*U)) {
                assert(VMap.count(CE) && "Mapping for ConstantExpr");
                auto NCE = handleCExpr(UCE, CE, VMap[CE]);
                VMap[UCE] = NCE;
                UpdateCExpr.push_back(UCE);
                AllCExpr.push_back(UCE);
            }
        }
    }

    for(auto &ACE : AllCExpr) {
        vector<User *> Users(ACE->user_begin(), ACE->user_end());
        for (auto &U : Users) {
            if(auto I = dyn_cast<Instruction>(U)) {
                assert(VMap[ACE] && "ConstantExpr not mapped");
                replaceIfOutlined(I, ACE, VMap[ACE]);
            }
        }
    }

    // Add return true for last block
    auto *BB = cast<BasicBlock>(VMap[RevTopoChop.front()]);
    BB->getTerminator()->eraseFromParent();
    ReturnInst::Create(Context, ConstantInt::getTrue(Context), BB);

    // Patch branches
    auto insertGuardCall = [&GuardFunc, &Context](BranchInst *CBR,
                                                  bool FreqCondition) {
        auto *Blk = CBR->getParent();
        Value *Arg = CBR->getCondition();
        Value *Dom = FreqCondition ? ConstantInt::getTrue(Context)
                                   : ConstantInt::getFalse(Context);
        vector<Value *> Params = {Arg, Dom};
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
            assert(false &&
                   "Switch instruction not handled, "
                   "use LowerSwitchPass to convert switch to if-else.");
        } else if (auto *BrInst = dyn_cast<BranchInst>(T)) {
            if (extractAsChop) {
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
                        if (inChop(T->getSuccessor(0))) {
                            insertGuardCall(BrInst, true);
                        } else {
                            insertGuardCall(BrInst, false);
                        }
                        T->eraseFromParent();
                        BranchInst::Create(cast<BasicBlock>(Targets[0]), NewBB);
                    }
                }
            } else {
                // Trace will replace the terminator inst with a direct branch
                // to the successor, the DCE pass will remove the comparison and
                // the simplification with merge the basic blocks later.
                if (T->getNumSuccessors() > 0) {
                    auto *SuccBB = *prev(IT);
                    vector<BasicBlock *> Succs(succ_begin(*IT), succ_end(*IT));
                    assert(find(Succs.begin(), Succs.end(), SuccBB) !=
                               Succs.end() &&
                           "Could not find successor!");
                    assert(VMap[SuccBB] && "Successor not found in VMap");
                    if (T->getNumSuccessors() == 2) {
                        if (T->getSuccessor(0) == SuccBB)
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

    auto handlePhis = [&VMap, &RevTopoChop, &BackEdges](PHINode *Phi,
                                                        bool extractAsChop) {
        auto NV = Phi->getNumIncomingValues();
        vector<BasicBlock *> ToRemove;
        for (unsigned I = 0; I < NV; I++) {
            auto *Blk = Phi->getIncomingBlock(I);
            auto *Val = Phi->getIncomingValue(I);

            if (!extractAsChop &&
                *next(find(RevTopoChop.begin(), RevTopoChop.end(),
                           Phi->getParent())) != Blk) {
                ToRemove.push_back(Blk);
                continue;
            }

            // Is this a backedge? Remove the incoming value
            // Is this predicated on a block outside the chop? Remove
            //assert(BackEdges.count(make_pair(Blk, Phi->getParent())) == 0 &&
                   //"Backedge Phi's should not exists -- should be promoted "
                   //"to LiveIn");

            if (find(RevTopoChop.begin(), RevTopoChop.end(), Blk) ==
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
            // Val may be constants or globals which are not present
            // in the map and don't need to be rewritten.
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
        errs() << "LO : " << *L << "\n";
        assert(LO && "Live Out not remapped");
        auto *Block = cast<Instruction>(LO)->getParent();
        Idx[1] = ConstantInt::get(Type::getInt32Ty(Context), OutIndex);
        // cast<PointerType>(Ptr->getType()->getScalarType())->getElementType()
        GetElementPtrInst *StructGEP = GetElementPtrInst::Create(
            cast<PointerType>(StructPtr->getType())->getElementType(),
            &*StructPtr, Idx, "", Block->getTerminator());
        auto *SI = new StoreInst(LO, StructGEP, Block->getTerminator());
        MDNode *N = MDNode::get(Context, MDString::get(Context, "true"));
        SI->setMetadata("LO", N);
        OutIndex++;
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

SmallVector<BasicBlock *, 16>
getTopoChop(DenseSet<BasicBlock *> &Chop, BasicBlock *StartBB,
            DenseSet<pair<const BasicBlock *, const BasicBlock *>> &BackEdges) {
    SmallVector<BasicBlock *, 16> Order;
    DenseSet<BasicBlock *> Seen;
    getTopoChopHelper(StartBB, Chop, Seen, Order, BackEdges);
    return Order;
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
                        common::checkIntrinsic(CS)) {
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

static StructType *getLiveOutStructType(SetVector<Value *> &LiveOut,
                                        Module *Mod) {
    SmallVector<Type *, 16> LiveOutTypes(LiveOut.size());
    transform(LiveOut.begin(), LiveOut.end(), LiveOutTypes.begin(),
              [](const Value *V) -> Type * { return V->getType(); });
    // Create a packed struct return type
    return StructType::get(Mod->getContext(), LiveOutTypes, true);
}

Function *MicroWorkloadExtract::extract(
    PostDominatorTree *PDT, Module *Mod,
    SmallVector<BasicBlock *, 16> &RevTopoChop, SetVector<Value *> &LiveIn,
    SetVector<Value *> &LiveOut, DominatorTree *DT, LoopInfo *LI) {

    auto *StartBB = RevTopoChop.back();
    auto *LastBB = RevTopoChop.front();

    auto BackEdges = common::getBackEdges(StartBB);
    auto ReachableFromLast = fSliceDFS(LastBB, BackEdges);

    assert(verifyChop(RevTopoChop) && "Invalid Region!");

    SetVector<Value *> Globals;

    auto handlePhiIn = [&LiveIn, &RevTopoChop, &Globals,
                        &StartBB](PHINode *Phi) {
        if (Phi->getParent() == StartBB) {
            LiveIn.insert(Phi);

        } else {
            for (uint32_t I = 0; I < Phi->getNumIncomingValues(); I++) {
                auto *Blk = Phi->getIncomingBlock(I);
                auto *Val = Phi->getIncomingValue(I);
                if (find(RevTopoChop.begin(), RevTopoChop.end(), Blk) !=
                    RevTopoChop.end()) {
                    if (auto *VI = dyn_cast<Instruction>(Val)) {
                        if (find(RevTopoChop.begin(), RevTopoChop.end(),
                                 VI->getParent()) == RevTopoChop.end()) {
                            LiveIn.insert(Val);
                        }
                    } else if (auto *AI = dyn_cast<Argument>(Val)) {
                        LiveIn.insert(AI);
                    } else if (auto *GV = dyn_cast<GlobalVariable>(Val)) {
                        Globals.insert(GV);
                    }
                }
            }
        }
    };

    // Live In Loop
    for (auto &BB : RevTopoChop) {
        for (auto &I : *BB) {
            if (auto Phi = dyn_cast<PHINode>(&I)) {
                handlePhiIn(Phi);
            } else {
                liveInHelper(RevTopoChop, LiveIn, Globals, &I);
            }
        }
    }

    errs() << "RevTopoChop : ";
    for (auto &B : RevTopoChop) {
        errs() << B->getName() << " ";
    }
    errs() << "\n";

    auto notInChop = [&RevTopoChop](const Instruction *I) -> bool {
        return find(RevTopoChop.begin(), RevTopoChop.end(), I->getParent()) ==
               RevTopoChop.end();
    };

    // Value is a live out only if it is used by an instruction
    // a. Reachable from the last block
    // b. As input itself (Induction Phis)
    // c. ??

    auto isLabelReachable = [&ReachableFromLast](
        const PHINode *Phi, const Instruction *Ins) -> bool {
        for (uint32_t I = 0; I < Phi->getNumIncomingValues(); I++) {
            if (Phi->getIncomingValue(I) == Ins &&
                ReachableFromLast.count(Phi->getIncomingBlock(I)))
                return true;
        }
        return false;
    };

    auto processLiveOut = [&LiveOut, &RevTopoChop, &StartBB, &LastBB, &LiveIn,
                           &notInChop, &DT, &LI, &ReachableFromLast,
                           &isLabelReachable](Instruction *Ins,
                                              Instruction *UIns) {
        if ( notInChop(UIns) && ( (!isa<PHINode>(UIns) && ReachableFromLast.count(UIns->getParent())) ||
                                  (isa<PHINode>(UIns) && isLabelReachable(cast<PHINode>(UIns), Ins))) ) {
            LiveOut.insert(Ins);
        } else if (LiveIn.count(UIns)) {
            LiveOut.insert(Ins);
        }
    };

    // Live Out Loop
    for (auto &BB : RevTopoChop) {
        for (auto &I : *BB) {
            // Don't consider Phi nodes in the first block
            // since we are not going to include them in
            // the extracted function anyway.
            if (isa<PHINode>(&I) && BB == StartBB)
                continue;
            if (auto Ins = dyn_cast<Instruction>(&I)) {
                for (auto UI = Ins->use_begin(), UE = Ins->use_end(); UI != UE;
                     UI++) {
                    if (auto UIns = dyn_cast<Instruction>(UI->getUser())) {
                        processLiveOut(Ins, UIns);
                    }
                }
            }
        }
    }


    auto isDefInOutlineBlocks = [&StartBB, &notInChop](Value* Val) -> bool {
        if( isa<Constant>(Val) ||
           (isa<Instruction>(Val) && notInChop(dyn_cast<Instruction>(Val))) ||
           (isa<PHINode>(Val) && dyn_cast<PHINode>(Val)->getParent() == StartBB) ) {
            return false;
        }
        return true;
    };

    // If LastT has 2 successors then, evaluate condition inside
    // and create a condition inside the success block to do the
    // same and branch accordingly.

    // If LastT has 1 successor then, the successor is a target
    // of the backedge from LastT, then nothing to do.

    // If the LastT has 0 successor then, there may be a return
    // value to patch.

    auto *LastT = LastBB->getTerminator();

    switch (LastT->getNumSuccessors()) {
    case 2: {
        auto *CBR = dyn_cast<BranchInst>(LastT);
        LiveOut.insert(CBR->getCondition());
    } break;
    case 1:
        break;
    case 0: {
        auto *RT = dyn_cast<ReturnInst>(LastT);
        assert(RT && "Path with 0 successor should have returninst");
        auto *Val = RT->getReturnValue();
        // This Val is added to the live out set only if it
        // is def'ed in the extracted region. 
        if ( Val != nullptr && isDefInOutlineBlocks(Val)) {
            LiveOut.insert(Val);
        }
    } break;
    default:
        assert(false && "Unexpected num successors -- lowerswitch?");
    }

    errs() << "LiveIns :\n";
    for (auto *V : LiveIn) {
        errs() << *V << "\n";
    }

    errs() << "LiveOut :\n";
    for (auto *V : LiveOut) {
        errs() << *V << "\n";
    }

    auto DataLayoutStr = Mod->getDataLayout();
    auto TargetTripleStr = StartBB->getParent()->getParent()->getTargetTriple();
    Mod->setDataLayout(DataLayoutStr);
    Mod->setTargetTriple(TargetTripleStr);

    // errs() << *StructPtrTy << "\n";

    // Bool return type for extracted function
    auto VoidTy = Type::getVoidTy(Mod->getContext());
    auto Int1Ty = IntegerType::getInt1Ty(Mod->getContext());

    std::vector<Type *> ParamTy;
    // Add the types of the input values
    // to the function's argument list
    for (auto Val : LiveIn)
        ParamTy.push_back(Val->getType());

    // if(LiveOut.size()) {
    // Create a packed struct return type
    auto *StructTy = getLiveOutStructType(LiveOut, Mod);
    auto *StructPtrTy = PointerType::getUnqual(StructTy);
    ParamTy.push_back(StructPtrTy);
    //}

    FunctionType *StFuncType = FunctionType::get(Int1Ty, ParamTy, false);

    // Create the trace function
    Function *StaticFunc =
        Function::Create(StFuncType, GlobalValue::ExternalLinkage,
                         "__offload_func_" + Mod->getName(), Mod);

    // Create an external function which is used to
    // model all guard checks. First arg is the condition, second is whether
    // the condition is dominant as true or as false. This
    // guard func is later replaced by a branch and return statement.
    // we use this as placeholder to create a superblock and enable
    // optimizations.
    ParamTy.clear();
    ParamTy = {Int1Ty, Int1Ty};
    FunctionType *GuFuncType = FunctionType::get(VoidTy, ParamTy, false);

    // Create the guard function
    Function *GuardFunc = Function::Create(
        GuFuncType, GlobalValue::ExternalLinkage, "__guard_func", Mod);

    extractHelper(StaticFunc, GuardFunc, LiveIn, LiveOut, Globals, RevTopoChop,
                  Mod->getContext());

    StripDebugInfo(*Mod);

    // Dumbass verifyModule function returns false if no
    // errors are found. Ref "llvm/IR/Verifier.h":46
    assert(!verifyModule(*Mod, &errs()) && "Module verification failed!");

    return StaticFunc;
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
        if (BlockMap.count(*RB) == 0)
            errs() << "Missing :" << *RB << "\n";
        assert(BlockMap.count(*RB) && "Path does not exist");
        RPath.push_back(BlockMap[*RB]);
    }
    return RPath;
}

static void instrument(Function &F, SmallVector<BasicBlock *, 16> &Blocks,
                       FunctionType *OffloadTy, SetVector<Value *> &LiveIn,
                       SetVector<Value *> &LiveOut, DominatorTree *DT,
                       string &Id) {
    // assert(Blocks.size() > 1 && "Can't extract unit block paths");
    if (Blocks.size() == 1) {
        auto *B = Blocks.front();
        auto *R = B->splitBasicBlock(B->getTerminator(), "unit.split");
        // errs() << "Unit Block Live In : \n";
        // for (auto &V : LiveIn)
        //     errs() << *V << "\n";
        // errs() << "Unit Block Live Out : \n";
        // for (auto &V : LiveOut)
        //     errs() << *V << "\n";
        Blocks.insert(Blocks.begin(), R);
    }

    // Setup Basic Control Flow
    BasicBlock *StartBB = Blocks.back(), *LastBB = Blocks.front();
    auto BackEdges = common::getBackEdges(StartBB);
    auto ReachableFromLast = fSliceDFS(LastBB, BackEdges);
    auto &Ctx = F.getContext();
    auto *Mod = F.getParent();
    auto *VoidTy = Type::getVoidTy(Ctx);
    auto *Int64Ty = Type::getInt64Ty(Ctx);
    auto *Int32Ty = Type::getInt32Ty(Ctx);
    auto *Success = BasicBlock::Create(Ctx, "offload.true", &F);

#ifdef PASHA_DEBUG
     if(SimulateDFG)
         CallInst::Create(Mod->getOrInsertFunction("__success", 
                 FunctionType::get(VoidTy, {}, false)), {}, "", Success);
#endif

    auto *Fail = BasicBlock::Create(Ctx, "offload.false", &F);
    auto *Merge = BasicBlock::Create(Ctx, "mergeblock", &F, nullptr);
    ConstantInt *Zero = ConstantInt::get(Int64Ty, 0);
    auto *Offload = cast<Function>(
        Mod->getOrInsertFunction("__offload_func_" + Id, OffloadTy));

    // Split the start basic block so that we can insert a call to the offloaded
    // function while maintaining the rest of the original CFG.
    auto *SSplit = StartBB->splitBasicBlock(StartBB->getFirstInsertionPt());
    SSplit->setName(StartBB->getName() + ".split");
    BranchInst::Create(Merge, Success);

    // Add a struct to the function entry block in order to
    // capture the live outs.
    auto InsertionPt = F.getEntryBlock().getFirstInsertionPt();
    auto *StructTy = getLiveOutStructType(LiveOut, Mod);
    auto *LOS = new AllocaInst(StructTy, "", &*InsertionPt);
    GetElementPtrInst *StPtr =
        GetElementPtrInst::CreateInBounds(LOS, {Zero}, "", &*InsertionPt);

    // Erase the branch of the split start block (this is always UBR).
    // Replace with a call to the offloaded function and then branch to
    // success / fail based on retval.
    StartBB->getTerminator()->eraseFromParent();
    vector<Value *> Params;
    for (auto &V : LiveIn)
        Params.push_back(V);
    Params.push_back(StPtr);
    auto *CI = CallInst::Create(Offload, Params, "", StartBB);
    BranchInst::Create(Success, Fail, CI, StartBB);

    // Divert control flow to pass through merge block from
    // original CFG.
    Merge->getInstList().push_back(LastBB->getTerminator()->clone());
    LastBB->getTerminator()->eraseFromParent();
    BranchInst::Create(Merge, LastBB);

    // Fail Path -- Begin
    ArrayType *LogArrTy = ArrayType::get(IntegerType::get(Ctx, 8), 0);
    // FIXME : These need to become internal to each new module
    auto *ULog = Mod->getOrInsertGlobal("__undo_log", LogArrTy);
    auto *NumStore = Mod->getOrInsertGlobal("__undo_num_stores",
                                            IntegerType::getInt32Ty(Ctx));
    Type *ParamTy[] = {Type::getInt8PtrTy(Ctx), Type::getInt32Ty(Ctx)};
    auto *UndoTy = FunctionType::get(Type::getVoidTy(Ctx), ParamTy, false);
    auto *Undo = Mod->getOrInsertFunction("__undo_mem", UndoTy);

    vector<Value *> Idx = {Zero, Zero};
    auto *UGEP = GetElementPtrInst::CreateInBounds(ULog, Idx, "", Fail);
    auto *UNS = GetElementPtrInst::CreateInBounds(NumStore, {Zero}, "", Fail);
    auto *NSLoad = new LoadInst(UNS, "", Fail);
    // Fail -- Undo memory
    vector<Value *> Args = {UGEP, NSLoad};
    CallInst::Create(Undo, Args, "", Fail);

#ifdef PASHA_DEBUG
     if(SimulateDFG)
         CallInst::Create(Mod->getOrInsertFunction("__fail", 
                 FunctionType::get(VoidTy, {}, false)), {}, "", Fail);
#endif    

    BranchInst::Create(SSplit, Fail);
    // Fail Path -- End

    // Update the Phi's in the targets of the merge block to use the merge
    // block instead of the LastBB. This needs to run before rewriting
    // uses since the rewriter has to have correct information
    // about the Phi's predecessor blocks in order to update the incorrect
    // values.
    auto updatePhis = [](BasicBlock *Tgt, BasicBlock *Old, BasicBlock *New) {
        for (auto &I : *Tgt) {
            if (auto *Phi = dyn_cast<PHINode>(&I)) {
                // errs() << *Phi << "\n";
                Phi->setIncomingBlock(Phi->getBasicBlockIndex(Old), New);
            }
        }
    };

    for (auto S = succ_begin(Merge), E = succ_end(Merge); S != E; S++) {
        updatePhis(*S, LastBB, Merge);
    }

    // Success Path - Begin
    // 1. Unpack the live out struct
    // 2. Merge live out values if required
    // 3. Rewrite Phi's in successor of LastBB
    //  a. Use merged values
    //  b. Use incoming block as Merge
    for (uint32_t Idx = 0; Idx < LiveOut.size(); Idx++) {
        auto *Val = LiveOut[Idx];
        // GEP Indices always need to i32 types.
        Value *StIdx = ConstantInt::get(Int32Ty, Idx, false);
        Value *GEPIdx[2] = {Zero, StIdx};
        auto *EValTy = cast<PointerType>(StPtr->getType())->getElementType();
        auto *ValPtr = GetElementPtrInst::Create(
            EValTy, StPtr, GEPIdx, "st_gep", Success->getTerminator());
        auto *Load = new LoadInst(ValPtr, "live_out", Success->getTerminator());

        // Merge the values -- Use original LiveOut if you came from
        // the LastBB. Use new loaded value if you came from the
        // offloaded function.
        auto *ValTy = Val->getType();
        auto *Phi = PHINode::Create(ValTy, 2, "", Merge->getTerminator());
        Phi->addIncoming(Val, LastBB);
        Phi->addIncoming(Load, Success);

        auto *Orig = dyn_cast<Instruction>(Val);
        assert(Orig && "Expect live outs to be instructions");

        SSAUpdater SSAU;
        SSAU.Initialize(Orig->getType(), Orig->getName());
        SSAU.AddAvailableValue(Orig->getParent(), Val);
        SSAU.AddAvailableValue(Merge, Phi);

        for (Value::use_iterator UI = Val->use_begin(), UE = Val->use_end();
             UI != UE;) {
            Use &U = *UI;
            ++UI;
            Instruction *UserInst = cast<Instruction>(U.getUser());
            BasicBlock *UserBB = UserInst->getParent();
            if (UserBB != Orig->getParent() &&
                !(UserBB == Merge && isa<PHINode>(UserInst))) {
                errs() << "Rewriting : " << *UserInst << "\n";
                SSAU.RewriteUseAfterInsertions(U);
            } else {
                errs() << "Not Rewriting : " << *UserInst << "\n";
            }
        }
    }
    // Success Path - End
}

//static void instrument(Function &F, SmallVector<BasicBlock *, 16> &Blocks,
                       //FunctionType *OffloadTy, SetVector<Value *> &LiveIn,
                       //SetVector<Value *> &LiveOut, DominatorTree *DT,
                       //mwe::PathType Type, string &Id) {
    //switch (Type) {
    //case FIRO:
    //case RIFO:
    //case RIRO:
    //case FIFO:
        //instrument(F, Blocks, OffloadTy, LiveIn, LiveOut, DT, Id);
        //break;
    //default:
        //assert(false && "Unexpected");
    //}
//}

static void runHelperPasses(Function *Offload, Function *Undo,
                            Module *Generated) {
    legacy::PassManager PM;
    PM.add(createBasicAAWrapperPass());
    PM.add(llvm::createTypeBasedAAWrapperPass());
    PM.add(new MicroWorkloadHelper(Offload, Undo));
    PM.run(*Generated);
}

void MicroWorkloadExtract::process(Function &F) {
    PostDomTree = &getAnalysis<PostDominatorTree>(F);
    auto *DT = &getAnalysis<DominatorTreeWrapperPass>(F).getDomTree();
    auto *LI = &getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();

    map<string, BasicBlock *> BlockMap;
    for (auto &BB : F)
        BlockMap[BB.getName().str()] = &BB;

    for (auto &P : Sequences) {

        ExtractedModules.push_back(
            llvm::make_unique<Module>(P.Id, getGlobalContext()));
        Module *Mod = ExtractedModules.back().get();
        Mod->setDataLayout(F.getParent()->getDataLayout());
        SmallVector<BasicBlock *, 16> Blocks =
            extractAsChop ? getChopBlocks(P, BlockMap)
                          : getTraceBlocks(P, BlockMap);

        // Extract the blocks and create a new function
        SetVector<Value *> LiveOut, LiveIn;
        Function *Offload =
            extract(PostDomTree, Mod, Blocks, LiveIn, LiveOut, DT, LI);

        // Creating a definition of the Undo function here and
        // then creating the body inside the pass causes LLVM to
        // crash thus nullptr is passed. CLEANME
        runHelperPasses(Offload, nullptr, Mod);

        instrument(F, Blocks, Offload->getFunctionType(), 
                LiveIn, LiveOut, DT, P.Id);

        //common::printCFG(F);
        //common::writeModule(Mod, (P.Id) + string(".ll"));
        //
        assert(!verifyModule(*Mod, &errs()) && "Module verification failed!");
    }
}

bool MicroWorkloadExtract::runOnModule(Module &M) {

    for (auto &F : M)
        if (isTargetFunction(F, FunctionList))
            process(F);

    return false;
}

char MicroWorkloadExtract::ID = 0;
