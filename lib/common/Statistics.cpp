#define DEBUG_TYPE "pasha_statistics"

#include "Common.h"
#include "llvm/ADT/SCCIterator.h"
#include <cassert>
#include <deque>
#include <fstream>
#include <string>

using namespace llvm;
using namespace std;
using namespace helpers;

static bool checkCall(const Instruction &I, string name) {
    if (isa<CallInst>(&I) && dyn_cast<CallInst>(&I)->getCalledFunction() &&
        dyn_cast<CallInst>(&I)->getCalledFunction()->getName().startswith(name))
        return true;
    return false;
}

/// Initializes the Opcode count
/// map and the Opcode weight map. New data fields need
/// to be initialized here. Additional Opcode counters may
/// also need to be initialized here.
bool Statistics::doInitialization(Module &M) {
    Data.clear();

#define HANDLE_INST(N, OPCODE, CLASS) OpcodeCount[#OPCODE] = 0;
#include "llvm/IR/Instruction.def"
    OpcodeCount["CondBr"] = 0;
    OpcodeCount["Guard"] = 0;

#define HANDLE_INST(N, OPCODE, CLASS) OpcodeWt[#OPCODE] = 1;
#include "llvm/IR/Instruction.def"

    /// Opcode weights can be overriden here to
    /// account for longer latency FP ops, MEM ops etc.

    return false;
}

static string getOpcodeStr(unsigned int N) {
    switch (N) {
#define HANDLE_INST(N, OPCODE, CLASS)                                          \
    case N:                                                                    \
        return string(#OPCODE);
#include "llvm/IR/Instruction.def"
    default:
        llvm_unreachable("Unknown Instruction");
    }
}

uint64_t Statistics::getBlockSize(BasicBlock *BB) {
    uint64_t Wt = 0;
    for (auto &I : *BB) {
        if (auto *SI = dyn_cast<StoreInst>(&I)) {
            if (SI->getMetadata("LO") != nullptr) {
                continue;
            }
        }
        Wt += OpcodeWt[getOpcodeStr(I.getOpcode())];
    }
    return Wt;
}

/// Compute the critical (longest) path from source (entry)
/// to sink (return true). The given function should be a
/// DAG. Uses the algorithm described in Wikipedia
/// https://en.wikipedia.org/wiki/Longest_path_problem
std::vector<std::pair<llvm::BasicBlock *, uint64_t>>
Statistics::criticalPath(Function &F) {

    std::vector<std::pair<llvm::BasicBlock *, uint64_t>> LongestPath;

    vector<BasicBlock *> TopoBlocks;
    for (auto I = scc_begin(&F), IE = scc_end(&F); I != IE; ++I) {
        if (I.hasLoop()) {
            llvm_unreachable("This should only run on DAGs");
        }
        TopoBlocks.insert(TopoBlocks.begin(), (*I).front());
    }

    assert(TopoBlocks.front() == &F.getEntryBlock() &&
           "Expect first block to be entry block");

    map<BasicBlock *, uint64_t> Distance;
    for (auto BB : TopoBlocks) {
        Distance[BB] = getBlockSize(BB);
        // Distance[BB] = BB->size();
        for (auto PB = pred_begin(BB), PE = pred_end(BB); PB != PE; PB++) {
            assert(Distance.count(*PB) &&
                   "This value should be initialized already");
            Distance[BB] += Distance[*PB];
        }
    }

    /// Decode the longest path by picking the predecessor with
    /// with the largest weight starting from the max value that
    /// has been computed.
    uint64_t Max = 0;
    BasicBlock *Last = nullptr;
    // errs() << "Distance\n";
    for (auto &BB : TopoBlocks) {
        // errs() << BB->getName() << " ";
        // errs() << Distance[BB] << "\n";
        auto Dist = Distance[BB];
        if (Max < Dist) {
            Max = Dist;
            Last = BB;
        }
    }

    // Data["crit-path-ops"] = Max;
    LongestPath.push_back({Last, Max});

    deque<BasicBlock *> Worklist;
    Worklist.push_back(Last);

    while (!Worklist.empty()) {
        auto *BB = Worklist.front();
        Worklist.pop_front();
        BasicBlock *Select = nullptr;
        uint64_t Max = 0;
        for (auto PB = pred_begin(BB), PE = pred_end(BB); PB != PE; PB++) {
            if (Distance[*PB] > Max) {
                Max = Distance[*PB];
                Select = *PB;
            }
        }
        if (Select) {
            Worklist.push_back(Select);
            LongestPath.push_back({Select, Distance[Select]});
        }
    }
    reverse(LongestPath.begin(), LongestPath.end());

    uint64_t CritPathSize = 0;
    for (auto &KV : LongestPath) {
        CritPathSize += getBlockSize(KV.first);
    }
    Data["crit-path-ops"] = CritPathSize;
    Data["crit-path-blocks"] = LongestPath.size();

    errs() << "LongestPath\n";
    for (auto &KV : LongestPath) {
        errs() << KV.first->getName() << " ";
    }
    errs() << "\n";

    return LongestPath;
}

/// Count per instruction statistics. Ignores store instructions
/// marked as live out with metadata checks. Also checks for
/// guard intrinsics as well as separating out conditional branches
/// from unconditional branches.
void Statistics::generalStats(Function &F) {

    for (auto &BB : F) {
        for (auto &I : BB) {

            // Also remove the GEPs required to index
            // into LO struct.
            if (auto *SI = dyn_cast<StoreInst>(&I)) {
                if (SI->getMetadata("LO") != nullptr) {
                    continue;
                }
            }

            switch (I.getOpcode()) {
#define HANDLE_INST(N, OPCODE, CLASS)                                          \
    case N:                                                                    \
        OpcodeCount[#OPCODE] += 1;                                             \
        break;
#include "llvm/IR/Instruction.def"
            }

            if (auto *BI = dyn_cast<BranchInst>(&I)) {
                if (BI->isConditional()) {
                    OpcodeCount["Br"] -= 1;
                    OpcodeCount["CondBr"] += 1;
                }
            }

            if (checkCall(I, "__guard_func")) {
                OpcodeCount["Call"] -= 1;
                OpcodeCount["Guard"] += 1;
            }
        }
    }
}

/// Detect if a Load or Store instruction is control
/// dependent. Starting from the BasicBlock of the
/// instruction, find a predecessor. If the predecessor
/// is unique then it is not control dependent. If the
/// search reaches the entry block then it is not control
/// dependent.
void Statistics::branchToMemoryDependency(Function &F) {
    uint64_t NumFound = 0;
    for (auto &BB : F) {
        for (auto &I : BB) {
            if (isa<LoadInst>(&I) || isa<StoreInst>(&I)) {
                auto *P = &BB;
                while (P && P != &F.getEntryBlock()) {
                    P = P->getUniquePredecessor();
                }

                if (P != &F.getEntryBlock()) {
                    NumFound++;
                }
            }
        }
    }

    Data["branch-mem-dep"] = NumFound;
}

/// For every branch in the function, find out if
/// the backward slice uses a load or store. Also
/// find the memory operations which are control
/// dependent on a branch.
void Statistics::memoryToBranchDependency(Function &F) {
    vector<User *> Slices;
    for (auto &BB : F) {
        for (auto &I : BB) {
            BranchInst *BI = dyn_cast<BranchInst>(&I);
            if (BI && BI->isConditional()) {
                Slices.push_back(BI);
            } else if (checkCall(I, "__guard_func")) {
                auto *CI = dyn_cast<CallInst>(&I);
                // auto *IC = dyn_cast<ICmpInst>(CI->getArgOperand(0));
                // assert(IC && "The first arg of guard should be ICmp");
                if (auto UI = dyn_cast<Instruction>(CI->getArgOperand(0)))
                    Slices.push_back(UI);
            }
        }
    }

    uint64_t NumFound = 0;
    for (auto *BI : Slices) {
        deque<User *> Worklist;
        bool Found = false;
        Worklist.push_back(BI);
        while (!Worklist.empty() && !Found) {
            auto *U = Worklist.front();
            Worklist.pop_front();
            for (auto OI = U->op_begin(), OE = U->op_end(); OI != OE; OI++) {
                if (dyn_cast<LoadInst>(OI)) {
                    Found = true;
                    NumFound++;
                    break;
                } else if (auto *UI = dyn_cast<User>(OI)) {
                    Worklist.push_back(UI);
                }
            }
        }

        if (Found) {
            // errs() << *BI << " is derived from load\n";
        }
        Found = false;
    }
    Data["mem-branch-dep"] = NumFound;
}

bool Statistics::runOnFunction(Function &F) {
    generalStats(F);
    // auto LongestPath = criticalPath(F);
    // memoryToBranchDependency(F);
    // branchToMemoryDependency(F);
    //

    ofstream Outfile((F.getName() + ".stats.txt").str(), ios::out);
    uint64_t TotalCount = 0;
    for (auto KV : OpcodeCount) {
        Outfile << KV.first << " " << KV.second << "\n";
        TotalCount += KV.second;
    }
    Outfile << "TotalOpsCount " << TotalCount << "\n";
    for (auto KV : Data) {
        Outfile << KV.first << " " << KV.second << "\n";
    }
    Outfile.close();
    return false;
}

bool Statistics::doFinalization(Module &M) { return false; }

char Statistics::ID = 0;
static RegisterPass<Statistics> X("", "Helpers - Statistics");
