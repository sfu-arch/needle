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

#define DEBUG_TYPE "mw"

using namespace llvm;
using namespace mwe;
using namespace std;

// Extern functions defined in MicroWorkloadExtract.cpp

extern SmallVector<BasicBlock *, 16>
getTopoChop(DenseSet<BasicBlock *> &Chop, BasicBlock *StartBB,
            DenseSet<pair<const BasicBlock *, const BasicBlock *>> &BackEdges);

// Static Functions 

static SmallVector<BasicBlock*, 16>
getFunctionRPO(Function& F) {
    DenseSet<BasicBlock*> Blocks;
    for(auto &BB : F) Blocks.insert(&BB);
    DenseSet<pair<const BasicBlock *, const BasicBlock *>> BackEdges;
    auto *StartBB = &F.getEntryBlock();
    
    auto RevOrder = getTopoChop(Blocks, StartBB, BackEdges);
    reverse(RevOrder.begin(), RevOrder.end());
    return RevOrder;
}

static void optimizeModule(Module *Mod) {
    PassManagerBuilder PMB;
    PMB.OptLevel = 3;
    PMB.SLPVectorize = false;
    PMB.BBVectorize = false;
    PassManager PM;
    PMB.populateModulePassManager(PM);
    PM.run(*Mod);
}

static bool
replaceGuardsHelper(Function& F,
                    BasicBlock* RetBlock,
                    Pass* P) {
    for(auto &BB : F) {
        for(auto &I : BB) {
            if(auto *CI = dyn_cast<CallInst>(&I)) {
                if(CI->getCalledFunction()->getName().equals("__guard_func")) {
                    // Arg 0 : The value to branch on
                    // Arg 1 : The dominant side of the branch (true or false)
                    Value *Arg0 = CI->getArgOperand(0);
                    auto *Arg1 = cast<ConstantInt>(CI->getArgOperand(1));
                    auto *NewBlock = SplitBlock(&BB, CI, P);
                    CI->eraseFromParent();
                    BB.getTerminator()->eraseFromParent();
                    if(Arg1->isOne()) {
                        BranchInst::Create(NewBlock, RetBlock, Arg0, &BB);
                    } else {
                        BranchInst::Create(RetBlock, NewBlock, Arg0, &BB);
                    }
                    return true;
                }
            }
        } 
    }
    return false;
}

// bool 
// __prev_store_exists(char* begin, char* loc) {
//     char *curr = loc - 16;
//     while(begin <= curr) {
//         if(*((uint64_t*)curr) == *((uint64_t*)loc)) {
//             return true;
//         }
//         curr -= 16;
//     }
//     return false;
// }

static void
storeCheckHelper(Function *F, Module *Mod) {
    auto &Ctx = Mod->getContext();
    auto Args = F->arg_begin();
    Value* PtrBegin = Args++;
    Value* PtrLoc = Args++;
    PtrBegin->setName("begin");
    PtrLoc->setName("loc");

    BasicBlock* Entry = BasicBlock::Create(Ctx, "entry",F,0);
    BasicBlock* WhileCond = BasicBlock::Create(Ctx, "while.cond",F,0);
    BasicBlock* WhileBody = BasicBlock::Create(Ctx, "while.body",F,0);
    BasicBlock* Return = BasicBlock::Create(Ctx, "return",F,0);

    auto *Int64Ty = Type::getInt64Ty(Ctx);
    
    // Constants
    auto *Zero = ConstantInt::get(Int64Ty, 0, false);
    auto *Minus16 = ConstantInt::get(Int64Ty, -16, true);

    // Entry
    auto* Loc = GetElementPtrInst::CreateInBounds(PtrLoc, {Zero}, "", Entry);
    auto *LocBC = new BitCastInst(Loc, PointerType::get(Int64Ty, 0), "", Entry);
    auto *LocVal = new LoadInst(LocBC, "", Entry);
    BranchInst::Create(WhileCond, Entry);

    // Cond
    auto *Phi = PHINode::Create(Loc->getType(), 2, "", WhileCond);
    Phi->addIncoming(Loc, Entry);
    auto* Curr = GetElementPtrInst::CreateInBounds(Phi, {Minus16}, "", WhileCond);
    auto* Begin = GetElementPtrInst::CreateInBounds(PtrBegin, {Zero}, "", WhileCond);
    auto* Cmp = new ICmpInst(*WhileCond, ICmpInst::ICMP_ULT, Curr, Begin, "");
    BranchInst::Create(WhileBody, Return, Cmp, WhileCond);

    // Body
    auto *CurrBC = new BitCastInst(Curr, PointerType::get(Int64Ty, 0), "", WhileBody);
    auto *CurrVal = new LoadInst(CurrBC, "", WhileBody);
    auto *ValCmp = new ICmpInst(*WhileBody, ICmpInst::ICMP_EQ, CurrVal, LocVal, "");
    BranchInst::Create(Return, WhileCond, ValCmp, WhileBody);
    Phi->addIncoming(Curr, WhileBody);

    // Return
    auto *RetVal = PHINode::Create(Type::getInt1Ty(Ctx), 2, "", Return);
    RetVal->addIncoming(ConstantInt::getTrue(Ctx), WhileBody);
    RetVal->addIncoming(ConstantInt::getFalse(Ctx), WhileCond);
    ReturnInst::Create(Ctx, RetVal, Return);
}

static void 
defineUndoFunction(Module* Mod, GlobalVariable* ULog, 
        Function *Undo, uint32_t Size) {
    auto &Ctx = Mod->getContext();

    // Create helper function which will check all prev
    // store addresses to see if it is the same, i.e linear
    // backward scan for uniqueness.
    
    auto *LogPtrType = ULog->getType();
    Type* ParamTy[] = {LogPtrType, LogPtrType};
    auto *Int1Ty = IntegerType::getInt1Ty(Ctx);
    auto *Fty = FunctionType::get(Int1Ty, ParamTy, false);

    auto *PrevStoreCheck = Function::Create(Fty, GlobalValue::InternalLinkage, 
                        "__prev_store_check", Mod);

    storeCheckHelper(PrevStoreCheck, Mod);

    auto *Entry = BasicBlock::Create(Ctx, "entry", Undo, nullptr);
    auto *Exit = BasicBlock::Create(Ctx, "exit", Undo, nullptr);
    auto *Body = BasicBlock::Create(Ctx, "body", Undo, nullptr);
    auto *Issue = BasicBlock::Create(Ctx, "issue", Undo, nullptr);
    auto *Tail = BasicBlock::Create(Ctx, "tail", Undo, nullptr);
    auto *TailA = BasicBlock::Create(Ctx, "tail.a", Undo, nullptr);
    auto *TailB = BasicBlock::Create(Ctx, "tail.b", Undo, nullptr);

    auto *Int64Ty = Type::getInt64Ty(Ctx);
    auto *Zero = ConstantInt::get(Int64Ty, 0, false);
    auto *Eight = ConstantInt::get(Int64Ty, 8, false);
    auto *Max = ConstantInt::get(Int64Ty, 2*8*Size);

    // Entry contents
    auto* BeginGEP = GetElementPtrInst::CreateInBounds(ULog, {Zero}, "begin", Entry);
    BranchInst::Create(Body, Entry);

    // Body block
    auto *Counter = PHINode::Create(Int64Ty, 2, "ctr", Body);
    Counter->addIncoming(Zero, Entry);
   
    auto* AddrGEP = GetElementPtrInst::CreateInBounds(ULog, {Counter}, "addr_gep", Body);
    auto* AddrBC = new BitCastInst(AddrGEP, PointerType::get(Int64Ty, 0), "", Body);
    auto* Addr = new LoadInst(AddrBC, "addr_ld", Body);
  
    auto* CounterPlusEight = BinaryOperator::CreateAdd(Counter, Eight, "", Body);
    auto* ValGEP = GetElementPtrInst::CreateInBounds(ULog, CounterPlusEight, "val_gep", Body);
    auto* ValBC = new BitCastInst(ValGEP, PointerType::get(Int64Ty, 0), "", Body);
    auto* Val = new LoadInst(ValBC, "val_ld", Body);

    auto* CounterPlusSixteen = BinaryOperator::CreateAdd(CounterPlusEight, Eight, "", Body);
    auto* Cond = new ICmpInst(*Body, ICmpInst::ICMP_ULT, Max, CounterPlusSixteen, "");
    BranchInst::Create(Exit, TailA, Cond, Body);
    Counter->addIncoming(CounterPlusSixteen, Tail);

    // Tail Blocks
    // a) If addr == 0, then dont do anything
    // b) Have we issued the store already? then don't do anything
     
    auto* CondA = new ICmpInst(*TailA, ICmpInst::ICMP_EQ, Addr, Zero, "");
    BranchInst::Create(Tail, TailB, CondA, TailA);

    Value* Args[2] = {BeginGEP, AddrGEP};
    auto *Check = CallInst::Create(PrevStoreCheck, Args, "chk", TailB);
    auto *CondB = new ICmpInst(*TailB, ICmpInst::ICMP_EQ, Check, ConstantInt::getTrue(Ctx), "");
    BranchInst::Create(Tail, Issue, CondB, TailB);

    auto* StAddr = new IntToPtrInst(Addr, PointerType::get(Int64Ty, 0), "", Issue);
    auto* StGEP = GetElementPtrInst::Create(StAddr, {Zero}, "st_gep", Issue);
    new StoreInst(Val, StGEP, Issue);
    BranchInst::Create(Tail, Issue);

    BranchInst::Create(Body, Tail);

    // Exit block contents
    ReturnInst::Create(Ctx, nullptr, Exit);
}

static Function* 
createUndoFunction(Module *Mod) {
    auto &Ctx = Mod->getContext();
    auto *VoidTy = Type::getVoidTy(Ctx);      
    auto *FlushTy = FunctionType::get(VoidTy, {}, false);
    auto *FlushFunc = Function::Create(FlushTy, GlobalValue::ExternalLinkage, 
                        "__undo_mem", Mod);
    return FlushFunc;
}

// Class Methods
void
MicroWorkloadHelper::addUndoLog() {
    // Get all the stores in the function minus the stores into 
    // the struct needed for live outs.
    // Create a new global variable, 2 words per store (addr+data)
    // save the address and data for each store. 
    // add a new function to the module which will flush 
    // the undo log
    Module *Mod = Offload->getParent();
    SmallVector<StoreInst*, 16> Stores;

    auto TopoBlocks = getFunctionRPO(*Offload);
    auto &AA = getAnalysis<AliasAnalysis>();

    auto isAliasingStore = [&AA, &Stores](StoreInst *SI) -> bool {
        for(auto &S : Stores) {
            if(AA.isMustAlias(AA.getLocation(SI), AA.getLocation(S)))
                return true;
        }
        return false;
    }; 
    
    for(auto &BB : TopoBlocks) {
        for(auto &I : *BB) {
            if(auto *SI = dyn_cast<StoreInst>(&I)) {
                // Filter out the stores added due to live outs
                // being returned as a struct by reference.
                if(SI->getMetadata("LO") == nullptr) {
                    if(!isAliasingStore(SI))
                        Stores.push_back(SI);
                }
            }
        }
    }

    // Create the Undo Log as a global variable
    ArrayType *LogArrTy =
        ArrayType::get(IntegerType::get(Mod->getContext(), 8), Stores.size()*2*8);
    auto *Initializer = ConstantAggregateZero::get(LogArrTy);
    GlobalVariable *ULog =
        new GlobalVariable(*Mod, LogArrTy, false,
        GlobalValue::CommonLinkage,
                           Initializer, "__undo_log");
    ULog->setAlignment(8);

    // Instrument the stores : 
    // a) Get the value from the load
    // b) Store the value+addr into the undo_log buffer
    
    uint32_t LogIndex = 0;
    Value* Idx[2];
    Idx[0] = ConstantInt::getNullValue(Type::getInt32Ty(Mod->getContext()));
    auto Int8Ty = Type::getInt8Ty(Mod->getContext());
    for(auto &SI : Stores) {
       auto *Ptr = SI->getPointerOperand();
       auto *LI = new LoadInst(Ptr, "undo", SI);
       Idx[1] = ConstantInt::get(Type::getInt32Ty(Mod->getContext()), LogIndex*8);
       LogIndex++;
       GetElementPtrInst *AddrGEP = GetElementPtrInst::Create(ULog, Idx, "", SI);
       auto *AddrBI = new PtrToIntInst(Ptr, Int8Ty, "", SI );
       new StoreInst(AddrBI, AddrGEP, SI);

       Idx[1] = ConstantInt::get(Type::getInt32Ty(Mod->getContext()), LogIndex*8);
       LogIndex++;
       GetElementPtrInst *ValGEP = GetElementPtrInst::Create(ULog, Idx, "", SI);
       auto *ValBI = new BitCastInst(ValGEP, PointerType::get(LI->getType(), 0), "", SI);
       new StoreInst(LI, ValBI, false, SI);
    }
    
    Undo = createUndoFunction(Mod);

    // Create a function to flush the undo log buffer
    defineUndoFunction(Mod, ULog, Undo, Stores.size());
    assert(!verifyModule(*Mod, &errs()) && "Module verification failed!");
}

void
MicroWorkloadHelper::replaceGuards() {
    auto &Context = Offload->getContext();
    auto *RetFalseBlock = BasicBlock::Create(Context, "ret.fail", Offload);
    ReturnInst::Create(Context, ConstantInt::getFalse(Context), RetFalseBlock);

    bool changed = false;
    while(replaceGuardsHelper(*Offload, RetFalseBlock, this)) changed = true;

    // No guard functions were found, so remove the basic
    // block we made.
    if(!changed) RetFalseBlock->eraseFromParent();

    auto *GuardFunc = Offload->getParent()->getFunction("__guard_func");
    assert(GuardFunc && "Guard Function definition not found");
    GuardFunc->eraseFromParent();
}


bool
MicroWorkloadHelper::runOnModule(Module& M) {
    optimizeModule(&M);
    //replaceGuards();
    addUndoLog();
    return false;
}

bool
MicroWorkloadHelper::doInitialization(Module& M) {
    return false;
}

bool
MicroWorkloadHelper::doFinalization(Module& M) {
    return false;
}

char MicroWorkloadHelper::ID = 0;
