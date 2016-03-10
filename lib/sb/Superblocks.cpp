#include "Superblocks.h"
#include <boost/algorithm/string.hpp>
#include "llvm/IR/Verifier.h"
#include "llvm/PassManager.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/DerivedTypes.h"
#include <fstream>

#define DEBUG_TYPE "pasha_sb"

using namespace llvm;
using namespace sb;
using namespace std;

extern cl::list<std::string> FunctionList;
extern bool isTargetFunction(const Function &f,
                             const cl::list<std::string> &FunctionList);

void Superblocks::readSequences() {
    ifstream SeqFile(SeqFilePath.c_str(), ios::in);
    assert(SeqFile.is_open() && "Could not open file");
    string Line;
    for (int64_t Count = 0; getline(SeqFile, Line);) {
        Path P;
        std::vector<std::string> Tokens;
        boost::split(Tokens, Line, boost::is_any_of("\t "));
        P.Id = Tokens[0];

        P.Freq = stoull(Tokens[1]);
        P.PType = static_cast<PathType>(stoi(Tokens[2]));

        move(Tokens.begin() + 4, Tokens.end() - 1, back_inserter(P.Seq));
        Sequences.push_back(P);
        errs() << *P.Seq.begin() << " " << *P.Seq.rbegin() << "\n";
        Count++;
        if (Count == NumSeq)
            break;
    }
    SeqFile.close();
}

void
Superblocks::makeEdgeProfile(map<string, BasicBlock*>& BM) {
    for(auto &P : Sequences) {
        auto &Blocks = P.Seq;
        for(unsigned I = 0; I < Blocks.size() - 1 ; I++) {
            auto E = make_pair(BM[Blocks[I]], BM[Blocks[I+1]]);
            if(EdgeProfile.count(E) == 0) {
                EdgeProfile.insert(make_pair(E, APInt(256, 0, false)));
            }
            EdgeProfile[E] += APInt(256, P.Freq, false);
        }
    } 
}

bool 
Superblocks::doInitialization(Module &M) {
    readSequences();
    return false;
}

bool 
Superblocks::doFinalization(Module &M) { return false; }

void 
Superblocks::process(Function &F) {
    map<string, BasicBlock *> BlockMap;
    for (auto &BB : F)
        BlockMap[BB.getName().str()] = &BB;

    makeEdgeProfile(BlockMap);

    for (auto &P : Sequences) {

    }
}

bool 
Superblocks::runOnModule(Module &M) {
    for (auto &F : M)
        if (isTargetFunction(F, FunctionList))
            process(F);

    return false;
}

char Superblocks::ID = 0;
