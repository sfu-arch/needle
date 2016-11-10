//===- InstruMem.cpp ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements an LLVM memory operation instrumentation pass.
//
//===----------------------------------------------------------------------===//

#include "InstruMem.h"

using namespace instrumem;

InstruMemPass::InstruMemPass() : FunctionPass(ID) {} // InstruMemPass

bool InstruMemPass::runOnFunction(Function &f) {

    visit(f);

    return true;
}

void InstruMemPass::visitFunction(Function &f) {
    Module *m = f.getParent();

    auto *voidTy = Type::getVoidTy(m->getContext());
    auto *i64Ty  = Type::getInt64Ty(m->getContext());
    i8PtrTy      = Type::getInt8PtrTy(m->getContext());
    auto *fTy    = Type::getFloatTy(m->getContext());
    auto *dTy    = Type::getDoubleTy(m->getContext());

    std::string pre = "__InstruMem_";
    onLoad =
        m->getOrInsertFunction(pre + "load", voidTy, i64Ty, i8PtrTy, i64Ty, nullptr);
    onStore =
        m->getOrInsertFunction(pre + "store", voidTy, i64Ty, i8PtrTy, i64Ty, nullptr);
}

void InstruMemPass::visitLoadInst(LoadInst &li) {

    int loadId = 0;

    auto *i64Ty = Type::getInt64Ty(li.getContext());

    Value *loaded   = li.getPointerOperand();
    BitCastInst *bc = new BitCastInst(loaded, i8PtrTy, "", &li);

    if (auto *N = dyn_cast<Instruction>(&li)->getMetadata("UID")) {
        auto *S = dyn_cast<MDString>(N->getOperand(0));
        loadId  = stoi(S->getString().str());
    } else
        assert(0);

    errs() << "instrumenting load " << loadId << "\n";

    if (!li.isTerminator()) {

        /*BasicBlock::iterator insrtPt = li;
        insrtPt++;
        Instruction* next = &*insrtPt;*/
        auto DL = li.getModule()->getDataLayout();  
        auto Sz = DL.getTypeStoreSize(cast<PointerType>(li.getPointerOperand()->getType())->getElementType());
        Value *args[] = {ConstantInt::get(i64Ty, loadId), bc, ConstantInt::get(i64Ty, Sz)};

        CallInst::Create(onLoad, args)->insertAfter(&li);
    } // load NOT at end of BB
    else {

        llvm_unreachable("ska124:this should be dead code");
        BasicBlock *parent = li.getParent();

        Value *args[] = {ConstantInt::get(i64Ty, loadId), bc};
        CallInst::Create(onLoad, args, "", parent);

    } // load at end of BB

} // visitLoad

void InstruMemPass::visitStoreInst(StoreInst &si) {

    int storeId = 0;

    auto *i64Ty = Type::getInt64Ty(si.getContext());

    Value *stored   = si.getPointerOperand();
    BitCastInst *bc = new BitCastInst(stored, i8PtrTy, "", &si);

    if (auto *N = dyn_cast<Instruction>(&si)->getMetadata("UID")) {
        auto *S = dyn_cast<MDString>(N->getOperand(0));
        storeId = stoi(S->getString().str());
    } else
        assert(0);

    errs() << "instrumenting store " << storeId << "\n";

    if (!si.isTerminator()) {

        /*BasicBlock::iterator insrtPt = si;
        insrtPt++;
        Instruction* next = &*insrtPt;*/

        auto DL = si.getModule()->getDataLayout();  
        auto Sz = DL.getTypeStoreSize(cast<PointerType>(si.getPointerOperand()->getType())->getElementType());
        Value *args[] = {ConstantInt::get(i64Ty, storeId), bc, ConstantInt::get(i64Ty, Sz)};

        CallInst::Create(onStore, args)->insertAfter(&si);
    } // store NOT at end of BB
    else {
        llvm_unreachable("ska124:this should be dead code");
        BasicBlock *parent = si.getParent();

        Value *args[] = {ConstantInt::get(i64Ty, storeId), bc};
        CallInst::Create(onStore, args, "", parent);

    } // store at end of BB

} // visitStore

char InstruMemPass::ID = 0;
static RegisterPass<InstruMemPass> X("instrumem", "InstruMem Pass");
