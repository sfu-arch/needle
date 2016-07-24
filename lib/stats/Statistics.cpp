#define DEBUG_TYPE "pasha_statistics"

#include "Statistics.h"
#include <cassert>
#include <string>
#include "json11.hpp"
#include "llvm/ADT/SCCIterator.h"
#include <deque>

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

/// Compute the critical (longest) path from source (entry)
/// to sink (return true). The given function should be a 
/// DAG. Uses the algorithm described in Wikipedia 
/// https://en.wikipedia.org/wiki/Longest_path_problem
void
Statistics::criticalPathLength(Function &F) {
    
    vector<BasicBlock*> TopoBlocks;
    for (auto I = scc_begin(&F),
        IE = scc_end(&F); I != IE; ++I) {    
        if(I.hasLoop()) {
            llvm_unreachable("This should only run on DAGs");
        }
        TopoBlocks.insert(TopoBlocks.begin(), (*I).front());
    }
   
    assert(TopoBlocks.front() == &F.getEntryBlock() &&
            "Expect first block to be entry block");
            

    map<BasicBlock*, uint64_t> Distance;
    for(auto &BB : TopoBlocks) {
        Distance[BB] = BB->size();  
        for(auto PB = pred_begin(BB), PE = pred_end(BB); 
                PB != PE; PB++) {
            assert(Distance.count(*PB) && 
                    "This value should be initialized already");
            Distance[BB] += Distance[*PB];
        }
    }
   
    /// Decode the longest path by picking the predecessor with
    /// with the largest weight starting from the max value that
    /// has been computed.
    //
    uint64_t Max = 0;
    BasicBlock* Last = nullptr;
    errs() << "Distance\n";
    for(auto &BB : TopoBlocks) {
        errs() << BB->getName() << " ";
        errs() << Distance[BB] << "\n";
        auto Dist = Distance[BB];
        if(Max < Dist) {
            Max = Dist;
            Last = BB;
        }
    }

    LongestPath.push_back({Last, Max});
   
    deque<BasicBlock*> Worklist;  
    Worklist.push_back(Last);

    while(!Worklist.empty()) {
        auto *BB = Worklist.front();
        Worklist.pop_front();
        BasicBlock *Select = nullptr;
        uint64_t Max = 0;
        for(auto PB = pred_begin(BB), PE = pred_end(BB); 
                        PB != PE; PB++) {
            if(Distance[*PB] > Max) {
                Max = Distance[*PB];
                Select = *PB;
            }
        }
        if(Select) {
            Worklist.push_back(Select);
            LongestPath.push_back({Select, Distance[Select]});
        }
    }
    reverse(LongestPath.begin(), LongestPath.end());

    errs() << "LongestPath\n";
    for(auto &KV : LongestPath) {
        errs() << KV.first->getName() << " ";
    }
}

void 
Statistics::releaseMemory() {
    LongestPath.clear();
    OpcodeCount.clear();
}

/// Count per instruction statistics. Ignores store instructions
/// marked as live out with metadata checks. Also checks for 
/// guard intrinsics as well as separating out conditional branches
/// from unconditional branches.
void 
Statistics::generalStats(Function &F) {
    for(auto &BB : F) {
        for(auto &I : BB) {

            // Also remove the GEPs required to index
            // into LO struct.
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


bool Statistics::runOnFunction(Function &F) {
    criticalPathLength(F);
    generalStats(F);
    return false;
}

bool Statistics::doFinalization(Module& M) {
    // uint64_t TotalCount = 0;
    // for(auto KV : OpcodeCount) {
    //     errs() << KV.first << " " 
    //            << KV.second << "\n";
    //     TotalCount += KV.second;
    // }    
    // errs() << "TotalCount " << TotalCount << "\n";
    return false;
}

char Statistics::ID = 0;
static RegisterPass<Statistics> X("", "PASHA - Statistics");
