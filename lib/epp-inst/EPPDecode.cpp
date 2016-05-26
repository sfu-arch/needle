#define DEBUG_TYPE "epp_decode"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/CallSite.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Constants.h"
#include "llvm/Analysis/LoopInfo.h"
#include <fstream>

#include <unordered_map>

#include "EPPDecode.h"
#include "Common.h"

using namespace llvm;
using namespace epp;
using namespace std;


extern cl::list<std::string> FunctionList;
extern bool isTargetFunction(const Function &, const cl::list<std::string> &);
extern cl::opt<std::string> profile;
extern cl::opt<bool> printSrcLines;

void printPath(std::vector<llvm::BasicBlock *> &Blocks,
               std::ofstream &Outfile) {
    for (auto *BB : Blocks) {
        DEBUG(errs() << BB->getName() << " ");
        Outfile << BB->getName().str() << " ";
    }
}

struct Path {
    Function *Func;
    APInt id;
    uint64_t count;
};

static bool isFunctionExiting(BasicBlock *BB) {
    if (BB->getTerminator()->getNumSuccessors() == 0)
        return true;
    return false;
}

static uint64_t pathCheck(vector<BasicBlock *> &Blocks) {
    // Check for un-acceleratable paths,
    // a) Indirect Function Calls
    // b) Function calls to external libraries
    // c) Memory allocations
    // return 0 if un-acceleratable or num_ins otherwise

    uint64_t NumIns = 0;
    for (auto BB : Blocks) {
        for (auto &I : *BB) {
            CallSite CS(&I);
            if (CS.isCall() || CS.isInvoke()) {
                if (!CS.getCalledFunction()) {
                    errs() << "Found indirect call\n";
                    return 0;
                } else {
                    if (CS.getCalledFunction()->isDeclaration() &&
                        common::checkIntrinsic(CS)) {
                        DEBUG(errs() << "Lib Call: "
                                     << CS.getCalledFunction()->getName()
                                     << "\n");
                        return 0;
                    }
                }
            }
        }
        uint64_t N = BB->getInstList().size();
        NumIns += N;
    }

    return NumIns;
}

void printPathSrc(std::vector<llvm::BasicBlock *> &blocks) {
    unsigned line = 0;
    llvm::StringRef file;
    for (auto *bb : blocks) {
        for (auto &instruction : *bb) {
            MDNode *n = instruction.getMetadata("dbg");
            if (!n) {
                continue;
            }
            DebugLoc Loc(n);
            if (Loc->getLine() != line || Loc->getFilename() != file) {
                line = Loc->getLine();
                file = Loc->getFilename();
                errs() << "File " << file.str() << " line " << line << "\n";
                // break; // FIXME : This makes it only print once for each BB,
                // remove to print all
                // source lines per instruction.
            }
        }
    }
}

bool EPPDecode::runOnModule(Module &M) {
    ifstream inFile(profile.c_str(), ios::in);
    assert(inFile.is_open() && "Could not open file for reading");

    uint64_t totalPathCount;
    inFile >> totalPathCount;

    std::vector<Path> paths;
    paths.reserve(totalPathCount);

    EPPEncode *Enc = nullptr;
    for (auto &F : M) {
        if (isTargetFunction(F, FunctionList)) {
            Enc = &getAnalysis<EPPEncode>(F);
            std::vector<uint64_t> counts(totalPathCount, 0);
            string PathIdStr;
            uint64_t PathCount;
            while (inFile >> PathIdStr >> PathCount) {
                APInt PathId(128, StringRef(PathIdStr), 16);
                paths.push_back({&F, PathId, PathCount});
            }
        }
    }
    inFile.close();

    for (auto &V : Enc->Val) {
        auto Src = V.first->src();
        if (ValBySrc.count(Src) == 0)
            ValBySrc[Src] = vector<shared_ptr<Edge>>();

        if (V.first->src() != V.first->tgt())
            ValBySrc[Src].push_back(V.first);
        // assert(V.first->src() != V.first->tgt() && "Noooo!");
    }


    // Sort the paths in descending order of their frequency
    // If the frequency is same, descending order of id (id cannot be same)
    std::sort(paths.begin(), paths.end(), [](const Path &P1, const Path &P2) {
        return (P1.count > P2.count) || 
            (P1.count == P2.count && P1.id.uge(P2.id));
    });

    std::vector<std::pair<PathType, 
        std::vector<llvm::BasicBlock *>>> bbSequences;
    bbSequences.reserve(totalPathCount);
    for (auto &path : paths) {
        bbSequences.push_back(decode(*path.Func, path.id, *Enc));
    }

    ofstream Outfile("epp-sequences2.txt", ios::out);

    uint64_t pathFail = 0;
    // Dump paths
    for (size_t i = 0, e = bbSequences.size(); i < e; ++i) {
        auto pType = bbSequences[i].first;
        int start = 0, end = 0;
        switch (pType) {
        case RIRO:
            break;
        case FIRO:
            start = 1;
            break;
        case RIFO:
            end = 1;
            break;
        case FIFO:
            start = 1;
            end = 1;
            break;
        }
        vector<BasicBlock *> blocks(bbSequences[i].second.begin() + start,
                                    bbSequences[i].second.end() - end);

        if (auto Count = pathCheck(blocks)) {
            DEBUG(errs() << i << " " << paths[i].count << " ");
            Outfile << paths[i].id.toString(10, false) << " " << paths[i].count
                    << " ";
            Outfile << static_cast<int>(pType) << " ";
            Outfile << Count << " ";
            printPath(blocks, Outfile);
            Outfile << "\n";
        } else {
            pathFail++;
            DEBUG(errs() << "Path Fail\n");
        }
        DEBUG(errs() << "Path ID: " << paths[i].id.toString(10, false)
               << " Freq: " << paths[i].count << "\n");
        if(printSrcLines)
            printPathSrc(blocks);
        DEBUG(errs() << "\n");
    }

    DEBUG(errs() << "Path Check Fails : " << pathFail << "\n");

    return false;
}

void EPPDecode::releaseMemory() {
    ValBySrc.clear();
}

std::pair<PathType, std::vector<llvm::BasicBlock *>>
EPPDecode::decode(Function &F, APInt pathID, EPPEncode &Enc) {
    std::vector<llvm::BasicBlock *> sequence;
    auto *Position = &F.getEntryBlock();

    DEBUG(errs() << "Decode Called On: " << pathID << "\n");

    vector<shared_ptr<Edge>> SelectedEdges;
    while (true) {
        sequence.push_back(Position);
        if (isFunctionExiting(Position))
            break;

        // Find the edge with the max weight <= pathID
        APInt Wt(128, 0, true);
        shared_ptr<Edge> Select = nullptr;
        for (auto &E : ValBySrc[Position]) {
            // Unsigned comparisons are OK since Vals are always +ve
            // Inc can be negative.
            if (Enc.Val[E].uge(Wt) && Enc.Val[E].ule(pathID)) {
                Select = E;
                Wt = Enc.Val[E];
            }
        }

        SelectedEdges.push_back(Select);
        Position = Select->tgt();
        pathID -= Wt;
        DEBUG(errs() << pathID << "\n");
    }

     //return make_pair(RIRO, sequence);
     //Only one path so it must be REAL
     if (SelectedEdges.empty()) {
         return make_pair(RIRO, sequence);
     }

enum EdgeType { EHEAD,
                ELATCH,
                ELIN,
                ELOUT1, // To Exit
                ELOUT2, // From Entry 
                EREAL,
                EOUT };

#define SET_BIT(n,x) (n |= 1ULL << x)
     uint64_t Type = 0;
     switch(SelectedEdges.front()->Type){
        case EHEAD:
        case ELOUT2:
             SET_BIT(Type, 0);
        default:
            break;
     }
     switch(SelectedEdges.back()->Type){
         case ELATCH:
         case ELOUT1:
         case ELIN:
            SET_BIT(Type, 1);
         default:
             break;
     }
#undef SET_BIT

     return make_pair(static_cast<PathType>(Type), sequence);

}

char EPPDecode::ID = 0;
static RegisterPass<EPPDecode> X("","PASHA - EPPDecode");
