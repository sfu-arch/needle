#define DEBUG_TYPE "pasha_common"

#include "llvm/Analysis/Passes.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "Common.h"

using namespace llvm;
using namespace helpers;


char DFGPrinter::ID = 0;

bool 
DFGPrinter::doInitialization(Module& M) {
    return false;
}

void 
DFGPrinter::visitFunction(Function& F){}

void 
DFGPrinter::visitBasicBlock(BasicBlock& BB){}

void 
DFGPrinter::visitInstruction(Instruction& I){}

bool 
DFGPrinter::doFinalization(Module& M ) {
    return false;
}

bool 
DFGPrinter::runOnFunction(Function& F ) {
    errs() << "Running DFGPrinter on " << F.getName() << "\n";
    return false;
}
