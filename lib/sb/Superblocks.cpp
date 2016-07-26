#define DEBUG_TYPE "pasha_superblock"
#include "Superblocks.h"
#include "Common.h"
#include "Statistics.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/TypeBasedAliasAnalysis.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include <boost/algorithm/string.hpp>
#include <fstream>

using namespace llvm;
using namespace sb;
using namespace std;

extern cl::list<std::string> FunctionList;
extern bool isTargetFunction(const Function &f,
                             const cl::list<std::string> &FunctionList);

namespace sb {
std::function<bool(const Edge &, const Edge &)> getCmp() {
    return [](const Edge &A, const Edge &B) -> bool {
        return A.first < B.first || (A.first == B.first && A.second < B.second);
    };
}
}

void Superblocks::readSequences() {
    ifstream SeqFile(SeqFilePath.c_str(), ios::in);
    assert(SeqFile.is_open() && "Could not open file");
    string Line;
    for (; getline(SeqFile, Line);) {
        Path P;
        std::vector<std::string> Tokens;
        boost::split(Tokens, Line, boost::is_any_of("\t "));
        P.Id = Tokens[0];

        P.Freq = stoull(Tokens[1]);
        P.PType = static_cast<PathType>(stoi(Tokens[2]));

        move(Tokens.begin() + 4, Tokens.end() - 1, back_inserter(P.Seq));
        Sequences.push_back(P);
        // errs() << *P.Seq.begin() << " " << *P.Seq.rbegin() << "\n";
    }
    SeqFile.close();
}

void Superblocks::makeEdgeProfile(map<string, BasicBlock *> &BM) {
    for (auto &P : Sequences) {
        auto &Blocks = P.Seq;
        for (unsigned I = 0; I < Blocks.size() - 1; I++) {
            auto E = make_pair(BM[Blocks[I]], BM[Blocks[I + 1]]);
            if (EdgeProfile.count(E) == 0) {
                EdgeProfile.insert(make_pair(E, APInt(256, 0, false)));
            }
            EdgeProfile[E] += APInt(256, P.Freq, false);
        }
    }
}

bool Superblocks::doInitialization(Module &M) {
    readSequences();
    return false;
}

bool Superblocks::doFinalization(Module &M) { 
    ofstream OutFile("superblocks.stat.txt", ios::out);
    for(auto KV: Data) {
        OutFile << KV.first << " " << KV.second << "\n";
    }
    return false; 
}

void Superblocks::construct(
    BasicBlock *Begin,
    SmallVector<SmallVector<BasicBlock *, 8>, 32> &Superblocks,
    DenseSet<pair<const BasicBlock *, const BasicBlock *>> &BackEdges) {
    BasicBlock *Next = nullptr, *Prev = nullptr;
    SmallVector<BasicBlock *, 8> SBlock;
    SBlock.push_back(Begin);
    Prev = Begin;

    auto hasEdgeToHeader = [&SBlock](const BasicBlock *Prev) -> bool {
        for (auto SB = succ_begin(Prev), SE = succ_end(Prev); SB != SE; SB++) {
            // If there is an edge back to the first block then exit
            if (SBlock[0] == *SB) {
                return true;
            }
        }
        return false;
    };

    do {
        if (Next != nullptr) {
            SBlock.push_back(Next);
            Prev = Next;
            Next = nullptr;
        }

        if (!hasEdgeToHeader(Prev)) {
            APInt Count(256, 0, false);
            for (auto SB = succ_begin(Prev), SE = succ_end(Prev); SB != SE;
                 SB++) {
                auto E = make_pair(Prev, *SB);
                if (EdgeProfile.count(E) != 0) {
                    if (EdgeProfile[E].ugt(Count)) {
                        Count = EdgeProfile[E];
                        Next = *SB;
                    }
                }
            }
        }
    } while (Next);

    if(SBlock.size() == 1) return;

    Superblocks.push_back(SBlock);

    uint64_t InsCount = 0, MemCount = 0;
    uint64_t ConditionCount = 0;
    vector<BasicBlock*> LoopBlocks;
    for(auto BB : SBlock) {
        LoopBlocks.push_back(BB);
        InsCount += (BB)->size();
        int32_t T = (BB)->getTerminator()->getNumSuccessors();
        ConditionCount += T - 1 > 0 ? T : 0;
        for(auto &I : *BB) {
            if(isa<LoadInst>(&I) || isa<StoreInst>(&I))
                MemCount++; 
        }
    }
    
    Data["superblock-"+to_string((uint64_t)Begin)+"-ins"     ] = InsCount; 
    Data["superblock-"+to_string((uint64_t)Begin)+"-bbcount" ] = LoopBlocks.size(); 
    Data["superblock-"+to_string((uint64_t)Begin)+"-cond"    ] = ConditionCount; 
    Data["superblock-"+to_string((uint64_t)Begin)+"-memcount"] = MemCount; 
}
void printPathSrc(SmallVector<llvm::BasicBlock *, 8> &blocks) {
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
                DEBUG(errs() << "File " << file.str() << " line " << line
                             << "\n");
                // break; // FIXME : This makes it only print once for each BB,
                // remove to print all
                // source lines per instruction.
            }
        }
    }
    //errs() << "-----------------------\n";
}



/// Compute the resource requirement of the Hyperblock which includes
/// the entire set of blocks of the innermost loop. Hyperblocks are 
/// constructed for loop which have been executed at least once. 
/// This is checked by looking at the edge profile for number of times 
/// and edge from the header to any successor is present.
void 
Superblocks::hyperblock(Loop* L, LoopInfo& LI) {
    auto *Header = L->getHeader(); 
    bool Found = false;
    for(auto SB = succ_begin(Header), SE = succ_end(Header); 
            SB != SE; SB++) {
        if(EdgeProfile.count({Header, *SB}))
            Found = true;
    }

    /// Don't do anything if this loop was never actually executed
    if(!Found) return;
  
    uint64_t InsCount = 0, MemCount = 0;
    uint64_t ConditionCount = 0;
    vector<BasicBlock*> LoopBlocks;
    for(auto BB = L->block_begin(), BE = L->block_end();
            BB != BE; BB++) {
        LoopBlocks.push_back(*BB);
        InsCount += (*BB)->size();
        int32_t T = (*BB)->getTerminator()->getNumSuccessors();
        ConditionCount += T - 1 > 0 ? T : 0;
        for(auto &I : **BB) {
            if(isa<LoadInst>(&I) || isa<StoreInst>(&I))
                MemCount++; 
        }
    }
    
    Data["hyperblock-"+to_string((uint64_t)Header)+"-ins"     ] = InsCount; 
    Data["hyperblock-"+to_string((uint64_t)Header)+"-bbcount" ] = LoopBlocks.size(); 
    Data["hyperblock-"+to_string((uint64_t)Header)+"-cond"    ] = ConditionCount; 
    Data["hyperblock-"+to_string((uint64_t)Header)+"-memcount"] = MemCount; 
}

void Superblocks::process(Function &F) {
    map<string, BasicBlock *> BlockMap;
    for (auto &BB : F)
        BlockMap[BB.getName().str()] = &BB;

    makeEdgeProfile(BlockMap);
    auto BackEdges = common::getBackEdges(&F.getEntryBlock());

    SmallVector<SmallVector<BasicBlock *, 8>, 32> Superblocks;
    LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();
    vector<Loop *> InnerLoops = getInnermostLoops(LI);
    DEBUG(errs() << "Num Loops: " << InnerLoops.size() << "\n");
    for (auto &L : InnerLoops) {
        assert(L->getHeader()->getParent() == &F);
        construct(L->getHeader(), Superblocks, BackEdges);
        hyperblock(L, LI);
    }

    std::ofstream edgefile("edgeprofile.txt", ios::out);
    for (auto &E : EdgeProfile) {
        edgefile << E.second.getZExtValue() << " "
                 << E.first.first->getName().str() << " "
                 << E.first.second->getName().str() << "\n";
    }
    edgefile.close();

    uint32_t Counter = 0;
    std::ofstream outfile("superblocks.txt", ios::out);

    for (auto &SV : Superblocks) {
        outfile << Counter++ << " 0 0 0 ";
        for (auto &BB : SV) {
            DEBUG(errs() << BB->getName() << " ");
            outfile << BB->getName().str() << " ";
        }
        outfile << "\n";
        DEBUG(errs() << "\n\n");
        //errs() << Counter - 1 << "\n";
        printPathSrc(SV);
    }
}

bool Superblocks::runOnModule(Module &M) {
    for (auto &F : M)
        if (isTargetFunction(F, FunctionList))
            process(F);

    return false;
}

char Superblocks::ID = 0;
