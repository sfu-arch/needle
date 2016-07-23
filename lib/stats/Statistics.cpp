#define DEBUG_TYPE "pasha_statistics"

#include "Statistics.h"
#include <cassert>
#include <string>
#include "json11.hpp"

using namespace llvm;
using namespace std;
using namespace pasha;

bool Statistics::doInitialization(Module &M) {
#define HANDLE_INST(N, OPCODE, CLASS)                                          \
    OpcodeCount[#OPCODE] = 0;
#include "llvm/IR/Instruction.def"
    OpcodeCount["CondBr"] = 0;
    OpcodeCount["Guard"] = 0;
    return false;
}

void
Statistics::criticalPathLength(Function &F) {
     
}

void 
Statistics::generalStats(Function &F) {
    for(auto &BB : F) {
        for(auto &I : BB) {

            if(auto *SI = dyn_cast<StoreInst>(&I)) {
                if (SI->getMetadata("LO") != nullptr) {
                    continue;
                }
            }
            
            switch(I.getOpcode()) {
#define HANDLE_INST(N, OPCODE, CLASS)           \
                case N:                         \
                    OpcodeCount[#OPCODE] += 1;  \
                    break;              
#include "llvm/IR/Instruction.def"
            }

            if(auto *BI = dyn_cast<BranchInst>(&I)) {
                if(BI->isConditional()) {
                    OpcodeCount["Br"] -= 1;
                    OpcodeCount["CondBr"] += 1;
                }
            }

            auto checkCall = [](const Instruction &I, string name) -> bool {
                if (isa<CallInst>(&I) && dyn_cast<CallInst>(&I)->getCalledFunction() &&
                    dyn_cast<CallInst>(&I)->getCalledFunction()->getName().startswith(
                        name))
                    return true;
                return false;
            };

            if (checkCall(I, "__guard_func")) {
                OpcodeCount["Call"] -= 1;
                OpcodeCount["Guard"] += 1;
            }
        }
    }
}

// Statistics :
// 1. Branch conditions based on memomry access
// 2. Ops only used for evaluation of conditions
// 3. Critical path length in ops

bool Statistics::runOnFunction(Function &F) {
    criticalPathLength(F);
    generalStats(F);
    return false;
}

bool Statistics::doFinalization(Module& M) {
    uint64_t TotalCount = 0;
    for(auto KV : OpcodeCount) {
        errs() << KV.first << " " 
               << KV.second << "\n";
        TotalCount += KV.second;
    }    
    errs() << "TotalCount " << TotalCount << "\n";
    return false;
}

char Statistics::ID = 0;
static RegisterPass<Statistics> X("", "PASHA - Statistics");
