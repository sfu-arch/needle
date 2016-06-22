
#ifndef INSTRUMEM_H
#define INSTRUMEM_H

#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/DataLayout.h"


using namespace llvm;



namespace instrumem{



struct InstruMemPass : public llvm::ModulePass, llvm::InstVisitor<InstruMemPass> {

	static char ID;
	
	int loadId = 0;
	



	InstruMemPass();

    virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<DataLayoutPass>();
    }


	bool runOnModule(llvm::Module &m) override;

	
	llvm::Type *i8PtrTy = nullptr;

	llvm::Constant *onLoad  = nullptr;

	


	void visitLoadInst(LoadInst &li);




};


} //namespace instrumem

#endif
