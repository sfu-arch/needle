#ifndef GRAPHGROK_H
#define GRAPHGROK_H

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"

#include <sstream>
#include <string>
#include <algorithm>
#include <fstream>
#include <set>
#include <map>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/breadth_first_search.hpp>
#include <boost/graph/graphviz.hpp>
#include <boost/bimap.hpp>

namespace grok {

enum VertexType {
    INT,
    FP,
    FUNC,
    INTRIN,
    GEP,
    UBR,
    CBR,
    SELECT,
    PHI,
    MEM_OTHER,
    MEM_ALLOCA,
    MEM_LD,
    MEM_ST,
    BB_START,
    RET,
    CONVERT,
    VECTOR,
    AGG,
    OTHER,
    CHAIN,
    NUM_VERTEX_TYPES
};
enum EdgeType { REG, DATA, NUM_EDGE_TYPES };

// Graph Defs using bundled properties
struct VertexProp {
    uint32_t Level;
    VertexType Type;
    llvm::Instruction *Inst;
    std::string Ops;
    uint32_t MyLatency;
};
struct EdgeProp {
    EdgeType Type;
};
typedef boost::adjacency_list<boost::listS, boost::vecS, boost::bidirectionalS,
                              VertexProp, EdgeProp> BoostGraph;
typedef boost::graph_traits<BoostGraph>::vertex_descriptor Vertex;
typedef boost::graph_traits<BoostGraph>::edge_descriptor Edge;
typedef boost::bimap<Vertex, llvm::Instruction *> bm_type;
typedef boost::bimap<Vertex, llvm::Instruction *>::value_type bm_index;

// R=Real F=Fake I=In O=Out
enum PathType { RIRO, FIRO, RIFO, FIFO, SELF };

struct Path {
    std::string Id;
    uint64_t Freq;
    PathType PType;
    std::vector<std::string> Seq;
    std::set<llvm::Value *> LiveIn, LiveOut, MemIn, MemOut, Globals;
};

struct GraphGrok : public llvm::ModulePass {
    static char ID;
    std::string SeqFilePath;
    int NumSeq;
    std::vector<Path> Sequences;
    // Sequence ID to Position in vector
    std::map<int64_t, int64_t> SequenceMap;
    llvm::PostDominatorTree *PostDomTree;
    llvm::AliasAnalysis *AA;
    std::ofstream StatsFile;

    GraphGrok(std::string S, int N)
        : llvm::ModulePass(ID), SeqFilePath(S), NumSeq(N) {}

    virtual bool runOnModule(llvm::Module &M) override;
    virtual bool doInitialization(llvm::Module &M) override;
    virtual bool doFinalization(llvm::Module &M) override;

    void readSequences(std::vector<Path> &S, std::map<int64_t, int64_t> &SM);

    void makeSeqGraph(llvm::Function &F);
    void makeBBPDG(llvm::BasicBlock &BB);
    void constructGraph(Path &P, BoostGraph &BG,
                        std::map<std::string, llvm::BasicBlock *> &BlockMap);

    virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const override {
        AU.addRequired<llvm::AliasAnalysis>();
        AU.addRequired<llvm::DominatorTreeWrapperPass>();
        AU.addRequired<llvm::PostDominatorTree>();
        AU.setPreservesAll();
    }
};
}

#endif
