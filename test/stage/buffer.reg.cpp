// Generated by llvm2cpp - DO NOT MODIFY!

#include <llvm/Pass.h>
#include <llvm/PassManager.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CallingConv.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRPrintingPasses.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/MathExtras.h>
#include <algorithm>
using namespace llvm;

Module* makeLLVMModule();

int main(int argc, char**argv) {
  Module* Mod = makeLLVMModule();
  verifyModule(*Mod, PrintMessageAction);
  PassManager PM;
  PM.add(createPrintModulePass(&outs()));
  PM.run(*Mod);
  return 0;
}


Module* makeLLVMModule() {
 // Module Construction
 Module* mod = new Module("buffer.reg.bc", getGlobalContext());
 mod->setDataLayout("0x34dd510");
 mod->setTargetTriple("x86_64-unknown-linux-gnu");
 
 // Type Definitions
 std::vector<Type*>FuncTy_0_args;
 PointerType* PointerTy_1 = PointerType::get(IntegerType::get(mod->getContext(), 8), 0);
 
 FuncTy_0_args.push_back(PointerTy_1);
 FuncTy_0_args.push_back(PointerTy_1);
 FunctionType* FuncTy_0 = FunctionType::get(
  /*Result=*/IntegerType::get(mod->getContext(), 32),
  /*Params=*/FuncTy_0_args,
  /*isVarArg=*/false);
 
 PointerType* PointerTy_2 = PointerType::get(IntegerType::get(mod->getContext(), 64), 0);
 
 
 // Function Declarations
 
 Function* func___prev_store_exists = mod->getFunction("__prev_store_exists");
 if (!func___prev_store_exists) {
 func___prev_store_exists = Function::Create(
  /*Type=*/FuncTy_0,
  /*Linkage=*/GlobalValue::ExternalLinkage,
  /*Name=*/"__prev_store_exists", mod); 
 func___prev_store_exists->setCallingConv(CallingConv::C);
 }
 AttributeSet func___prev_store_exists_PAL;
 {
  SmallVector<AttributeSet, 4> Attrs;
  AttributeSet PAS;
   {
    AttrBuilder B;
    B.addAttribute(Attribute::NoUnwind);
    B.addAttribute(Attribute::UWTable);
    PAS = AttributeSet::get(mod->getContext(), ~0U, B);
   }
  
  Attrs.push_back(PAS);
  func___prev_store_exists_PAL = AttributeSet::get(mod->getContext(), Attrs);
  
 }
 func___prev_store_exists->setAttributes(func___prev_store_exists_PAL);
 
 // Global Variable Declarations

 
 // Constant Definitions
 ConstantInt* const_int64_3 = ConstantInt::get(mod->getContext(), APInt(64, StringRef("-16"), 10));
 ConstantInt* const_int32_4 = ConstantInt::get(mod->getContext(), APInt(32, StringRef("1"), 10));
 ConstantInt* const_int32_5 = ConstantInt::get(mod->getContext(), APInt(32, StringRef("0"), 10));
 
 // Global Variable Definitions
 
 // Function Definitions
 
 // Function: __prev_store_exists (func___prev_store_exists)
 {
  Function::arg_iterator args = func___prev_store_exists->arg_begin();
  Value* ptr_begin = args++;
  ptr_begin->setName("begin");
  Value* ptr_loc = args++;
  ptr_loc->setName("loc");
  
  BasicBlock* label_entry = BasicBlock::Create(mod->getContext(), "entry",func___prev_store_exists,0);
  BasicBlock* label_while_cond = BasicBlock::Create(mod->getContext(), "while.cond",func___prev_store_exists,0);
  BasicBlock* label_while_body = BasicBlock::Create(mod->getContext(), "while.body",func___prev_store_exists,0);
  BasicBlock* label_if_then = BasicBlock::Create(mod->getContext(), "if.then",func___prev_store_exists,0);
  BasicBlock* label_if_end = BasicBlock::Create(mod->getContext(), "if.end",func___prev_store_exists,0);
  BasicBlock* label_while_end = BasicBlock::Create(mod->getContext(), "while.end",func___prev_store_exists,0);
  BasicBlock* label_return = BasicBlock::Create(mod->getContext(), "return",func___prev_store_exists,0);
  
  // Block entry (label_entry)
  GetElementPtrInst* ptr_add_ptr = GetElementPtrInst::Create(ptr_loc, const_int64_3, "add.ptr", label_entry);
  BranchInst::Create(label_while_cond, label_entry);
  
  // Block while.cond (label_while_cond)
  Argument* fwdref_7 = new Argument(PointerTy_1);
  PHINode* ptr_curr_0 = PHINode::Create(PointerTy_1, 2, "curr.0", label_while_cond);
  ptr_curr_0->addIncoming(ptr_add_ptr, label_entry);
  ptr_curr_0->addIncoming(fwdref_7, label_if_end);
  
  ICmpInst* int1_cmp = new ICmpInst(*label_while_cond, ICmpInst::ICMP_ULE, ptr_begin, ptr_curr_0, "cmp");
  BranchInst::Create(label_while_body, label_while_end, int1_cmp, label_while_cond);
  
  // Block while.body (label_while_body)
  CastInst* ptr_9 = new BitCastInst(ptr_curr_0, PointerTy_2, "", label_while_body);
  LoadInst* int64_10 = new LoadInst(ptr_9, "", false, label_while_body);
  int64_10->setAlignment(8);
  CastInst* ptr_11 = new BitCastInst(ptr_loc, PointerTy_2, "", label_while_body);
  LoadInst* int64_12 = new LoadInst(ptr_11, "", false, label_while_body);
  int64_12->setAlignment(8);
  ICmpInst* int1_cmp1 = new ICmpInst(*label_while_body, ICmpInst::ICMP_EQ, int64_10, int64_12, "cmp1");
  BranchInst::Create(label_if_then, label_if_end, int1_cmp1, label_while_body);
  
  // Block if.then (label_if_then)
  BranchInst::Create(label_return, label_if_then);
  
  // Block if.end (label_if_end)
  GetElementPtrInst* ptr_add_ptr2 = GetElementPtrInst::Create(ptr_curr_0, const_int64_3, "add.ptr2", label_if_end);
  BranchInst::Create(label_while_cond, label_if_end);
  
  // Block while.end (label_while_end)
  BranchInst::Create(label_return, label_while_end);
  
  // Block return (label_return)
  PHINode* int32_retval_0 = PHINode::Create(IntegerType::get(mod->getContext(), 32), 2, "retval.0", label_return);
  int32_retval_0->addIncoming(const_int32_4, label_if_then);
  int32_retval_0->addIncoming(const_int32_5, label_while_end);
  
  ReturnInst::Create(mod->getContext(), int32_retval_0, label_return);
  
  // Resolve Forward References
  fwdref_7->replaceAllUsesWith(ptr_add_ptr2); delete fwdref_7;
  
 }
 
 return mod;
}