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




InstruMemPass::InstruMemPass()
	:ModulePass(ID) {

}//InstruMemPass


bool
InstruMemPass::runOnModule(Module &m) {
  

	visit(m);

	return true;
}




void InstruMemPass::visitModule(Module& m) {
	auto *voidTy = Type::getVoidTy(m.getContext());
	auto *i64Ty  = Type::getInt64Ty(m.getContext());
	i8PtrTy      = Type::getInt8PtrTy(m.getContext());
	auto* fTy = Type::getFloatTy(m.getContext());
	auto* dTy = Type::getDoubleTy(m.getContext());

	std::string pre = "InstruMem_";
	onLoad  = m.getOrInsertFunction(pre+"load", voidTy, i64Ty, i8PtrTy, nullptr);
	
}




void InstruMemPass::visitLoadInst(LoadInst &li) {

	uint64_t size    = getAnalysis<DataLayoutPass>().getDataLayout().getTypeStoreSize(li.getType());
	auto  *i64Ty     = Type::getInt64Ty(li.getContext());

	Value *loaded = li.getPointerOperand();
	BitCastInst* bc = new BitCastInst(loaded, i8PtrTy, "", &li);


		loadId ++;

    if (!li.isTerminator()) {

        BasicBlock::iterator insrtPt = li;
        insrtPt++;
        Instruction* next = &*insrtPt;			
    
    
        Value *args[] = {
            ConstantInt::get(i64Ty, loadId),
            bc};
        
        };	
        CallInst::Create(onLoad, args, "", next);	
    }//load NOT at end of BB
    else {

        BasicBlock* parent = li.getParent();

        Value *args[] = {
            ConstantInt::get(i64Ty, loadId),
            bc};	
        CallInst::Create(onLoad, args, "", parent);				

    }//load at end of BB




}//visitLoad









char InstruMemPass::ID = 0;
static RegisterPass<InstruMemPass> X("instrumem", "InstruMem Pass");


