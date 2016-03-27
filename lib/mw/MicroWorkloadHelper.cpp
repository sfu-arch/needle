#include "MicroWorkloadExtract.h"
#include <boost/algorithm/string.hpp>
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

static Function* 
createUndoFunction(Module *Mod) {
    auto &Ctx = Mod->getContext();
    auto *VoidTy = Type::getVoidTy(Ctx);      
    Type *ParamTy[] = {Type::getInt8PtrTy(Ctx), Type::getInt32Ty(Ctx)};
    auto *FlushTy = FunctionType::get(VoidTy, ParamTy, false);
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
    auto &Ctx = Mod->getContext();
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
        ArrayType::get(IntegerType::get(Ctx, 8), Stores.size()*2*8);
    auto *Initializer = ConstantAggregateZero::get(LogArrTy);
    GlobalVariable *ULog =
        new GlobalVariable(*Mod, LogArrTy, false,
        GlobalValue::ExternalLinkage,
                           Initializer, "__undo_log");
    ULog->setAlignment(8);

    // Save the number of stores as a Global Variable as well
    auto *Int32Ty = IntegerType::getInt32Ty(Ctx);
    auto *NumStores = ConstantInt::get(Int32Ty, Stores.size(), 0);
    new GlobalVariable(*Mod, Int32Ty, false, GlobalValue::ExternalLinkage,
            NumStores, "__undo_num_stores");

    // Instrument the stores : 
    // a) Get the value from the load
    // b) Store the value+addr into the undo_log buffer
    
    uint32_t LogIndex = 0;
    Value* Idx[2];
    Idx[0] = ConstantInt::getNullValue(Type::getInt32Ty(Ctx));
    auto Int8Ty = Type::getInt8Ty(Ctx);
    auto *Int64Ty = Type::getInt64Ty(Ctx);
    //auto *Debug = Mod->getFunction("__debug_log");
    for(auto &SI : Stores) {
       auto *Ptr = SI->getPointerOperand();
       auto *LI = new LoadInst(Ptr, "undo", SI);
       Idx[1] = ConstantInt::get(Type::getInt32Ty(Ctx), LogIndex*8);
       LogIndex++;
       GetElementPtrInst *AddrGEP = GetElementPtrInst::Create(ULog, Idx, "", SI);
       auto *AddrCast = new PtrToIntInst(Ptr, Int64Ty, "", SI );
       auto *AddrBI = new BitCastInst(AddrGEP, PointerType::getInt64PtrTy(Ctx), "", SI);
       new StoreInst(AddrCast, AddrBI, SI);

       Idx[1] = ConstantInt::get(Type::getInt32Ty(Ctx), LogIndex*8);
       LogIndex++;
       GetElementPtrInst *ValGEP = GetElementPtrInst::Create(ULog, Idx, "", SI);
       auto *ValBI = new BitCastInst(ValGEP, PointerType::get(LI->getType(),0), "", SI);
       new StoreInst(LI, ValBI, false, SI);
       //vector<Value*> Args = {AddrGEP, Zero};
       //CallInst::Create(Debug, Args, "")->insertAfter(SI);
    }
    
    createUndoFunction(Mod);

    // Create a function to flush the undo log buffer
    // defineUndoFunction(Mod, ULog, Undo, Stores.size());

    // Add a buffer init memset into the offload function entry
    Type *Tys[] = { Type::getInt8PtrTy(Ctx), Type::getInt32Ty(Ctx) };
    auto *Memset = Intrinsic::getDeclaration(Mod, Intrinsic::memset, Tys);
    auto *Zero = ConstantInt::get(Int64Ty, 0, false);
    Idx[1] = Zero;
    auto *UGEP = GetElementPtrInst::Create(ULog, Idx, "", 
            Offload->getEntryBlock().getFirstNonPHI());
    Value* Params[] = {UGEP, ConstantInt::get(Int8Ty, 0, false), 
                        ConstantInt::get(Type::getInt32Ty(Ctx), Stores.size()*2*8, false), 
                        ConstantInt::get(Type::getInt32Ty(Ctx), 8, false), 
                        ConstantInt::getFalse(Ctx)};
    CallInst::Create(Memset, Params, "")->insertAfter(UGEP);

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
    //optimizeModule(&M);
    replaceGuards();
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
