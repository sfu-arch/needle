#ifndef LOOPCONVERTER_H
#define LOOPCONVERTER_H
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Dominators.h" 
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"

#include <cassert>

using namespace llvm;
using namespace std;

// Convert self loops to normal loops, this makes the 
// segmentation easier and also cleans up other special
// cases.
namespace pasha {

struct LoopConverter : public ModulePass {
    static char ID; 

    LoopConverter() : ModulePass(ID) {}
    
    bool doInitialization(Module &M) { 
        return false;
    }

    bool doFinalization(Module &M) {
        return false;
    }

    bool runOnModule(Module &M);

    void getAnalysisUsage(AnalysisUsage &AU) const {
    }

};

}

#endif
