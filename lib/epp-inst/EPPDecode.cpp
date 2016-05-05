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
extern cl::opt<std::string> selfloop;

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

    bool operator<(const Path &other) const { return count < other.count; }

    // Path() : Func(nullptr), id(APInt(256, StringRef("0"), 10)), count(0) {}
};

static bool isFunctionExiting(BasicBlock *BB) {
    if (BB->getTerminator()->getNumSuccessors() == 0)
        return true;

    // TODO: Check for exception handling
    // TODO: Check for functions that don't return

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
        assert(NumIns + N > NumIns && "Overflow check failed");
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

            DILocation loc(n);
            if (loc.getLineNumber() != line || loc.getFilename() != file) {
                line = loc.getLineNumber();
                file = loc.getFilename();
                DEBUG(errs() << "File " << file.str() << " line " << line << "\n");
                // break; // FIXME : This makes it only print once for each BB,
                // remove to print all
                // source lines per instruction.
            }
        }
    }
}

bool EPPDecode::runOnModule(Module &M) {
    // FILE *infile = fopen(ProfileDumpFile.c_str(), "r");
    ifstream inFile(profile.c_str(), ios::in);
    assert(inFile.is_open() && "Could not open file for reading");

    uint64_t totalPathCount;
    // fread(&totalPathCount, sizeof(totalPathCount), 1, infile);

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
                APInt PathId(256, StringRef(PathIdStr), 10);
                paths.push_back({&F, PathId, PathCount});
            }
        }
    }

    inFile.close();

    ifstream inFile2(selfloop.c_str(), ios::in);
    assert(inFile2.is_open() && "Could not open file for reading");
    uint64_t SelfLoopId, Freq, totalSelfLoopCount;
    inFile2 >> totalSelfLoopCount;
    while (inFile2 >> SelfLoopId >> Freq) {
        SelfProfileMap[SelfLoopId] = Freq;
    }
    inFile2.close();

    // Sort the paths in descending order of their frequency
    std::sort(paths.begin(), paths.end(), [](const Path &P1, const Path &P2) {
        return P1.count > P2.count;
    });

    std::vector<std::pair<PathType, std::vector<llvm::BasicBlock *>>>
        bbSequences;
    bbSequences.reserve(totalPathCount);
    for (auto &path : paths) {
        bbSequences.push_back(decode(*path.Func, path.id, *Enc));
    }

    ofstream Outfile("epp-sequences.txt", ios::out);

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
            // case SELF : break; // Does not exist yet.
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
        //printPathSrc(blocks);
        DEBUG(errs() << "\n");
    }

    DEBUG(errs() << "Path Check Fails : " << pathFail << "\n");

    // Dump self loops (if any)
    for (auto KV : SelfProfileMap) {
        auto Id = KV.first;
        auto Count = KV.second;
        assert(Enc->selfLoopMap.count(Id) > 0 && "Self Loop not found");
        vector<BasicBlock *> Path;
        Path.push_back(Enc->selfLoopMap[Id]);
        auto C = pathCheck(Path);
        if (C)
            Outfile << (totalPathCount + 1 + Id) << " " << Count << " 4 " << C
                    << " " << Path[0]->getName().str() << " \n";
    }
    return false;
}

std::pair<PathType, std::vector<llvm::BasicBlock *>>
EPPDecode::decode(Function &F, APInt pathID, EPPEncode &Enc) {
    std::vector<llvm::BasicBlock *> sequence;
    auto *Position = &F.getEntryBlock();

    DEBUG(errs() << "Decode Called On: " << pathID << "\n");

    // Data structure for faster lookup
    map<BasicBlock *, vector<shared_ptr<Edge>>> ValBySrc;
    for (auto &V : Enc.Val) {
        auto Src = V.first->src();
        if (ValBySrc.count(Src) == 0)
            ValBySrc[Src] = vector<shared_ptr<Edge>>();

        if (V.first->src() != V.first->tgt())
            ValBySrc[Src].push_back(V.first);
        // assert(V.first->src() != V.first->tgt() && "Noooo!");
    }

    // FIXME : This should only happen once 
    errs() << "Created loopup structure\n";

    vector<shared_ptr<Edge>> SelectedEdges;
    while (true) {
        sequence.push_back(Position);
        if (isFunctionExiting(Position))
            break;

        // Find the edge with the max weight <= pathID
        APInt Wt(256, StringRef("0"), 10);
        shared_ptr<Edge> Select = nullptr;
        // for(auto &E : getEdgesBySource(Position, Enc.Val))
        for (auto &E : ValBySrc[Position]) {
            // if(Enc.Val[E] >= Wt && Enc.Val[E] <= pathID)
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

     if (SelectedEdges.front()->Type == EREAL &&
         SelectedEdges.back()->Type == EREAL)
         return make_pair(RIRO, sequence);

     if (SelectedEdges.front()->Type != EREAL &&
         SelectedEdges.back()->Type == EREAL)
         return make_pair(FIRO, sequence);

     if (SelectedEdges.front()->Type == EREAL &&
         SelectedEdges.back()->Type != EREAL )
         return make_pair(RIFO, sequence);

     if (SelectedEdges.front()->Type != EREAL &&
         SelectedEdges.back()->Type != EREAL)
         return make_pair(FIFO, sequence);

    assert(false && "This should be unreachable");
    return make_pair(FIFO, sequence);
}

char EPPDecode::ID = 0;
static RegisterPass<EPPDecode> X("","PASHA - EPPDecode");
