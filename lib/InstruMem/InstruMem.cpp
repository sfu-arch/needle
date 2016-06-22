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
	onFunEntry  = m.getOrInsertFunction(pre+"funEntry", voidTy, nullptr);
	onFunExit   = m.getOrInsertFunction(pre+"funExit", voidTy, nullptr);
	onLoad  = m.getOrInsertFunction(pre+"load", voidTy, i64Ty, i8PtrTy, i64Ty, i64Ty, nullptr);
	onFPLoad  = m.getOrInsertFunction(pre+"FPload", voidTy, i64Ty, i8PtrTy, fTy, i64Ty, nullptr);
	onFP2Load  = m.getOrInsertFunction(pre+"FP2load", voidTy, i64Ty, i8PtrTy, dTy, i64Ty, nullptr);
	
}

void InstruMemPass::visitFunction(Function &f) {

	if (f.isDeclaration()) return;


	CallInst::Create(onFunEntry, "", &*(f.getEntryBlock().getFirstInsertionPt()));

}//InstruMemPass::visitFunction


void InstruMemPass::visitReturnInst(ReturnInst &ri) { 
	
	

	CallInst::Create(onFunExit, "", &ri);
	
}//InstruMemPass::visitReturnInst

void InstruMemPass::visitLoadInst(LoadInst &li) {

	uint64_t size    = getAnalysis<DataLayoutPass>().getDataLayout().getTypeStoreSize(li.getType());
	auto  *i64Ty     = Type::getInt64Ty(li.getContext());

	Value *loaded = li.getPointerOperand();
	BitCastInst* bc = new BitCastInst(loaded, i8PtrTy, "", &li);

	if (li.getType()->isIntegerTy()){

		loadId ++;

		if (!li.isTerminator()) {
		
			BasicBlock::iterator insrtPt = li;
			insrtPt++;
			Instruction* next = &*insrtPt;			
		
		
			Value *args[] = {
				ConstantInt::get(i64Ty, loadId),
				bc,
				new ZExtInst(&li, i64Ty, "", next),
				ConstantInt::get(i64Ty, size)
			
	  		};	
			CallInst::Create(onLoad, args, "", next);	
		}//load NOT at end of BB
		else {

			BasicBlock* parent = li.getParent();

			Value *args[] = {
				ConstantInt::get(i64Ty, loadId),
				bc,
				new ZExtInst(&li, i64Ty, "", parent),
				ConstantInt::get(i64Ty, size)
			
	  		};	
			CallInst::Create(onLoad, args, "", parent);				

		}//load at end of BB

	}//integer load
	else if(li.getType()->isFloatTy()){ 

		loadId ++;

		if (!li.isTerminator()) {
		
			BasicBlock::iterator insrtPt = li;
			insrtPt++;
			Instruction* next = &*insrtPt;			
		
		
			Value *args[] = {
				ConstantInt::get(i64Ty, loadId),
				//new FPExtInst(&li, i8PtrTy, "", next),
				bc,
				&li,
	    			ConstantInt::get(i64Ty, size)
			
	  		};	
			CallInst::Create(onFPLoad, args, "", next);	
		}//load NOT at end of BB
		else {

			BasicBlock* parent = li.getParent();

			Value *args[] = {
				ConstantInt::get(i64Ty, loadId),
				//new BitCastInst(&li, i8PtrTy, "", parent),
				bc,
				&li,
	    			ConstantInt::get(i64Ty, size)
			
	  		};	
			CallInst::Create(onFPLoad, args, "", parent);				

		}//load at end of BB

	}//float load

	else if(li.getType()->isDoubleTy()){ 

		loadId ++;

		if (!li.isTerminator()) {
		
			BasicBlock::iterator insrtPt = li;
			insrtPt++;
			Instruction* next = &*insrtPt;			
		
		
			Value *args[] = {
				ConstantInt::get(i64Ty, loadId),
				//new FPExtInst(&li, i8PtrTy, "", next),
				bc,
				&li,
	    			ConstantInt::get(i64Ty, size)
			
	  		};	
			CallInst::Create(onFP2Load, args, "", next);	
		}//load NOT at end of BB
		else {

			BasicBlock* parent = li.getParent();

			Value *args[] = {
				ConstantInt::get(i64Ty, loadId),
				//new BitCastInst(&li, i8PtrTy, "", parent),
				bc,
				&li,
	    			ConstantInt::get(i64Ty, size)
			
	  		};	
			CallInst::Create(onFP2Load, args, "", parent);				

		}//load at end of BB

	}//double load



}//visitLoad









char InstruMemPass::ID = 0;
static RegisterPass<InstruMemPass> X("instrumem", "InstruMem Pass");


