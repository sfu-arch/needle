#define DEBUG_TYPE "grok"
#include "GraphGrok.h"
#include <boost/algorithm/string.hpp>
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include "llvm/IR/Verifier.h"
#include "llvm/PassManager.h"
#include <boost/graph/breadth_first_search.hpp>
#include "llvm/IR/BasicBlock.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/DenseSet.h"
#include "DilworthDecomposition.h"
#include <boost/graph/bipartite.hpp>
#include <boost/graph/strong_components.hpp>
#include <boost/property_map/property_map.hpp>
#include <cxxabi.h>

#include <map>
#include <set>

using namespace llvm;
using namespace grok;
using namespace std;

const char *VertexTypeStr[NUM_VERTEX_TYPES] = {
    "INT",     "FP",     "FUNC", "INTRIN", "GEP",    "UBR",      "CBR",
    "SELECT",  "PHI",    "MEM",  "MEM_LD", "MEM_ST", "BB_START", "RET",
    "CONVERT", "VECTOR", "AGG",  "OTHER",  "CHAIN"};
const char *EdgeTypeStr[NUM_EDGE_TYPES] = {"REG", "DATA"};

static_assert(NUM_VERTEX_TYPES ==
                  sizeof(VertexTypeStr) / sizeof(VertexTypeStr[0]),
              "Unequal number of Vertex Types and string descriptions");
static_assert(NUM_EDGE_TYPES == sizeof(EdgeTypeStr) / sizeof(EdgeTypeStr[0]),
              "Unequal number of Edge Types and string descriptions");

extern cl::list<std::string> FunctionList;
extern bool isTargetFunction(const Function &f,
                      const cl::list<std::string> &FunctionList);

//extern cl::opt<string> TargetFunction; 

//cl::opt<int> MaxNumPaths("max", cl::desc("Maximum number of paths to analyse"),
                         //cl::value_desc("Integer"), cl::init(10));
                         
cl::opt<string>
    GenerateTrace("trace", cl::desc("Generate bitcode trace from path graph"),
                  cl::value_desc("static/dynamic"), cl::init("unset"));

//cl::opt<string> OnlyPath("path", cl::desc("Run on only specified path"),
                         //cl::value_desc("String"), cl::init("na"));

VertexType getOpType(Instruction &I);

void GraphGrok::readSequences(vector<Path> &S, map<int64_t, int64_t> &SM) {
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

        // Token[3] is the number of instructions in path.
        // Not needed here. It's there in the file for filtering
        // and finding out exactly how many paths we want to
        // analyse.

        // a. The last token is blank, so always end -1
        // b. If Path is RIRO, then range is +4, -1
        // c. If Path is FIRO, then range is +5, -1
        // d. If Path is RIFO, then range is +4, -2
        // e. If Path is FIFO, then range is +5, -2
        // f. If Path is SELF, then range is +4, -1
        switch (P.PType) {
        case RIRO:
            move(Tokens.begin() + 4, Tokens.end() - 1, back_inserter(P.Seq));
            break;
        case FIRO:
            move(Tokens.begin() + 5, Tokens.end() - 1, back_inserter(P.Seq));
            break;
        case RIFO:
            move(Tokens.begin() + 4, Tokens.end() - 2, back_inserter(P.Seq));
            break;
        case FIFO:
            move(Tokens.begin() + 5, Tokens.end() - 2, back_inserter(P.Seq));
            break;
        case SELF:
            move(Tokens.begin() + 4, Tokens.end() - 1, back_inserter(P.Seq));
            break;
        default:
            assert(false && "Unknown path type");
        }
        S.push_back(P);
        // SM[P.Id] = Count;
        Count++;
        if (Count == NumSeq)
            break;
    }
    SeqFile.close();
}

bool GraphGrok::doInitialization(Module &M) {
    readSequences(Sequences, SequenceMap);
    return false;
}

bool GraphGrok::doFinalization(Module &M) { return false; }

// Edge Writer helper class

template <class PropertyMap> class edge_writer {
  public:
    edge_writer(PropertyMap w) : pm(w) {}
    template <class Edge> void operator()(ostream &out, const Edge &e) const {
        // out << "[label=\"" << EdgeTypeStr[pm[e]];
        // out << "[";
        // if(pm[e] == REG )
        //     out << "style=\"solid\"]";
        // else
        //     out << "style=\"dashed\"]";
    }

  private:
    PropertyMap pm;
};

template <class PropertyMap>
inline edge_writer<PropertyMap> make_edge_writer(PropertyMap w) {
    return edge_writer<PropertyMap>(w);
}

// Vertex Writer helper class

template <class TypeMap, class LevelMap, class OpMap, class LatencyMap>
class vertex_writer {
  public:
    vertex_writer(TypeMap TM, LevelMap LM, OpMap OM, LatencyMap LTM)
        : TMap(TM), LMap(LM), OMap(OM), LTMap(LTM) {}
    template <class Vertex>
    void operator()(ostream &out, const Vertex &v) const {
        out << " [";
        // Write out readable label --
        // to be used for visualization
        out << "label=\"" << to_string(v) << " "
            << " (" << to_string(LMap[v]) << ") " << VertexTypeStr[TMap[v]]
            << "\" ";
        // Write custom attr
        out << "mytype=" << TMap[v] << " "
            << ",mylevel=" << to_string(LMap[v]) << " "
            << ",myops=" << OMap[v] << " "
            << ",mylatency=" << LTMap[v] << "]";
    }

  private:
    TypeMap TMap;
    LevelMap LMap;
    OpMap OMap;
    LatencyMap LTMap;
};

template <class TypeMap, class LevelMap, class OpMap, class LatencyMap>
inline vertex_writer<TypeMap, LevelMap, OpMap, LatencyMap>
make_vertex_writer(TypeMap w, LevelMap x, OpMap o, LatencyMap l) {
    return vertex_writer<TypeMap, LevelMap, OpMap, LatencyMap>(w, x, o, l);
}

static bool isBlockInPath(const string &S, const Path &P) {
    return find(P.Seq.begin(), P.Seq.end(), S) != P.Seq.end();
}

static inline bool isUnconditionalBranch(Instruction *I) {
    if (auto BRI = dyn_cast<BranchInst>(I))
        return BRI->isUnconditional();
    return false;
}

static inline uint32_t getBlockIdx(const Path &P, BasicBlock *BB) {
    auto Name = BB->getName().str();
    uint32_t Idx = 0;
    for (; Idx < P.Seq.size(); Idx++)
        if (Name == P.Seq[Idx])
            return Idx;
    assert(false && "Should be Unreachable");
}

void GraphGrok::constructGraph(Path &P, BoostGraph &BG,
                               map<string, BasicBlock *> &BlockMap) {
    Vertex CurrV, BBStart;
    BBStart = boost::add_vertex(
        {0, BB_START, nullptr, VertexTypeStr[BB_START], 0}, BG);
    bm_type VertexInsMap;
    DEBUG(errs() << "Path " << P.Id << " " << P.PType << "\n");

    // Add all the vertices
    for (auto &BlockName : P.Seq) {
        auto *BB = BlockMap[BlockName];
        DEBUG(errs() << BlockName << "\n");
        for (auto &Ins : *BB) {
            if (isa<DbgInfoIntrinsic>(&Ins) || isUnconditionalBranch(&Ins) ||
                isa<ReturnInst>(&Ins))
                continue;

            auto Type = getOpType(Ins);
            CurrV =
                boost::add_vertex({0, Type, &Ins, VertexTypeStr[Type], 0}, BG);
            VertexInsMap.insert(bm_index(CurrV, &Ins));
            DEBUG(errs() << CurrV << " " << Ins << "\n");
        }
    }

    // Add all the edges
    // for(auto &BlockName : P.Seq)
    for (uint32_t BlockIdx = 0; BlockIdx < P.Seq.size(); BlockIdx++) {
        auto BlockName = P.Seq[BlockIdx];
        auto *BB = BlockMap[BlockName];
        for (auto &Ins : *BB) {
            if (isa<DbgInfoIntrinsic>(&Ins) || isUnconditionalBranch(&Ins) ||
                isa<ReturnInst>(&Ins))
                continue;

            auto IS = VertexInsMap.right.find(&Ins);
            assert(IS != VertexInsMap.right.end() && "Not found in BiMap");
            auto CurrV = IS->second;

            bool DepsFound = false;
            for (auto OI = Ins.op_begin(), E = Ins.op_end(); OI != E; ++OI) {
                if (auto II = dyn_cast<Instruction>(OI)) {
                    auto IT = VertexInsMap.right.find(II);
                    if (IT == VertexInsMap.right.end())
                        continue;

                    // This could assert if the instruction which the phi gets
                    // its value from is outside the path, but that case is
                    // handled
                    // by the previous if statement.
                    auto OperandBlockIdx =
                        getBlockIdx(P, IT->first->getParent());

                    // Phis
                    if (auto Phi = dyn_cast<PHINode>(&Ins)) {
                        // Handle self loop phis
                        if (OperandBlockIdx == BlockIdx)
                            continue;
                        // For Phi, check the operand block should be prev block
                        auto IncomingBlock = Phi->getIncomingBlock(*OI);
                        if (!isBlockInPath(IncomingBlock->getName().str(), P))
                            continue;
                        if (!(getBlockIdx(P, IncomingBlock) == BlockIdx - 1))
                            continue;
                    }
                    // Only add an edge if the operand occurs in the same block
                    // or in a block before this in the path. This should
                    // eliminate
                    // all backward edges if no other instructions apart from
                    // PHINodes
                    // can have Operands which occur after it.
                    assert(OperandBlockIdx <= BlockIdx &&
                           "Instruction has operand which occurs in path after "
                           "it!");
                    DepsFound = true;
                    auto Vx = IT->second;
                    boost::add_edge(Vx, CurrV, {REG}, BG);
                }
            }

            if (!DepsFound)
                add_edge(BBStart, CurrV, {REG}, BG);
        }
    }
}

void writeGraph(const Path &P, const BoostGraph &BG, const string &Prepend) {
    string DotFileName(P.Id + string(".") + Prepend + string(".dot"));
    ofstream DotFile(DotFileName.c_str(), ios::out);
    boost::write_graphviz(
        DotFile, BG, make_vertex_writer(boost::get(&VertexProp::Type, BG),
                                        boost::get(&VertexProp::Level, BG),
                                        boost::get(&VertexProp::Ops, BG),
                                        boost::get(&VertexProp::MyLatency, BG)),
        make_edge_writer(boost::get(&EdgeProp::Type, BG)));
    DotFile.close();
}

static void dfsTraverseHelper(Vertex V, BoostGraph &BG,
                              vector<Vertex> &Vertices, set<Vertex> &Seen) {
    Seen.insert(V);
    auto EI = out_edges(V, BG);
    for (auto EBegin = EI.first, EEnd = EI.second; EBegin != EEnd; EBegin++) {
        if (!Seen.count(target(*EBegin, BG)))
            dfsTraverseHelper(target(*EBegin, BG), BG, Vertices, Seen);
    }
    Vertices.push_back(V);
}

static vector<Vertex> dfsTraverse(BoostGraph &BG) {
    vector<Vertex> Vertices;
    set<Vertex> Seen;
    dfsTraverseHelper(0 /* Start from root vertex */, BG, Vertices, Seen);
    return Vertices;
}

static inline set<Vertex> getParents(const Vertex V, const BoostGraph BG) {
    set<Vertex> Parents;
    auto EI = in_edges(V, BG);
    for (auto EB = EI.first; EB != EI.second; EB++)
        Parents.insert(source(*EB, BG));
    return Parents;
}

static inline set<Vertex> getChildren(const Vertex V, const BoostGraph BG) {
    set<Vertex> Children;
    auto EI = out_edges(V, BG);
    for (auto EB = EI.first; EB != EI.second; EB++)
        Children.insert(target(*EB, BG));
    return Children;
}

static void removePHI(Path &P, BoostGraph &BG,
                      map<string, BasicBlock *> &BlockMap, ofstream &Out) {
    map<Instruction *, Vertex> VertexInsMap;
    BGL_FORALL_VERTICES(V, BG, BoostGraph) { VertexInsMap[BG[V].Inst] = V; }

    uint32_t PhisRemoved = 0, PhiIn = 0;

    // BGL_FORALL_VERTICES(V, BG, BoostGraph)
    for (auto V : dfsTraverse(BG)) {
        if (BG[V].Type == PHI) {
            auto Parents = getParents(V, BG);
            auto Phi = dyn_cast<PHINode>(BG[V].Inst);
            auto Name = Phi->getParent()->getName().str();

            if (Name == P.Seq.front()) {
                // This is the first block, difficult to resolve which
                // value to choose, can't pick one since if it is a constant
                // which is reused it will screw up. (eg. 2974 in reco) had
                // constant 0 which was also used in an inbounds GEP. Instead
                // we promote the Phi node itself to an input.
                auto *RewriteVal = dyn_cast<Value>(Phi);
                P.LiveIn.insert(RewriteVal);

                // Update children, wire edge from 0->Child,
                // update used value for Child Instruction.
                for (auto Child : getChildren(V, BG)) {
                    add_edge(0, Child, {REG}, BG);
                    BG[Child].Inst->replaceUsesOfWith(Phi, RewriteVal);
                }

                // Is this Phi a live-out ? Then rewrite the LiveOut
                // auto LO = find(P.LiveOut.begin(), P.LiveOut.end(), Phi);
                // if( LO != P.LiveOut.end()) {
                // P.LiveOut.erase(LO);
                // P.LiveOut.insert(RewriteVal);
                //                    //errs() << __LINE__ << " " << *RewriteVal
                //                    << "\n";
                //}

                PhiIn++;
            } else {
                // Phi value came from preceding block
                // a) a constant (wire to BB_START)
                // b) an instruction

                auto PrevBlock =
                    BlockMap[*prev(find(P.Seq.begin(), P.Seq.end(), Name))];
                bool Found = false;
                for (uint32_t I = 0; I < Phi->getNumIncomingValues(); I++) {
                    auto *Val = Phi->getIncomingValue(I);
                    auto *Blk = Phi->getIncomingBlock(I);

                    if (PrevBlock == Blk) {
                        if (auto Ins = dyn_cast<Instruction>(Val)) {
                            if (isBlockInPath(Ins->getParent()->getName().str(),
                                              P)) {
                                // Wire to Producer-Ins
                                auto W = VertexInsMap[Ins];
                                auto *RewriteVal = dyn_cast<Value>(BG[W].Inst);
                                for (auto Child : getChildren(V, BG)) {
                                    add_edge(W, Child, {REG}, BG);
                                    BG[Child].Inst->replaceUsesOfWith(
                                        Phi, RewriteVal);
                                }

                                // Is this Phi a live-out ? Then rewrite the
                                // LiveOut
                                auto LO = find(P.LiveOut.begin(),
                                               P.LiveOut.end(), Phi);
                                if (LO != P.LiveOut.end()) {
                                    P.LiveOut.erase(LO);
                                    P.LiveOut.insert(RewriteVal);
                                    //                    errs() << __LINE__ <<
                                    //                    " " << *RewriteVal <<
                                    //                    "\n";
                                }
                            } else {
                                // %sum_u.1 = phi i32 [ %add, %if.then25  ] ...
                                // Say %add is not in %if.then25.
                                // Is this possible?
                                // Wire to BB_START
                                // assert(false && "Can this happen?");
                                // Update : Yep, Path 2188 in reco.
                                auto *RewriteVal = dyn_cast<Value>(Ins);
                                P.LiveIn.insert(RewriteVal);
                                for (auto Child : getChildren(V, BG)) {
                                    add_edge(0, Child, {REG}, BG);
                                    BG[Child].Inst->replaceUsesOfWith(
                                        Phi, RewriteVal);
                                }
                                PhiIn++;

                                // Is this Phi a live-out ? Then rewrite the
                                // LiveOut
                                auto LO = find(P.LiveOut.begin(),
                                               P.LiveOut.end(), Phi);
                                if (LO != P.LiveOut.end()) {
                                    P.LiveOut.erase(LO);
                                    P.LiveOut.insert(RewriteVal);
                                    //                    errs() << __LINE__ <<
                                    //                    " " << *RewriteVal <<
                                    //                    "\n";
                                }
                            }
                        } else {
                            // Constant value
                            // Wire to BB_START
                            auto *RewriteVal = dyn_cast<Value>(Phi);
                            P.LiveIn.insert(RewriteVal);
                            for (auto Child : getChildren(V, BG)) {
                                add_edge(0, Child, {REG}, BG);
                                BG[Child].Inst->replaceUsesOfWith(Phi,
                                                                  RewriteVal);
                            }

                            // errs() << "Rewrite phi with constant\n" << *Phi
                            // << "\n";

                            // Is this Phi a live-out ? Then rewrite the LiveOut
                            auto LO =
                                find(P.LiveOut.begin(), P.LiveOut.end(), Phi);
                            if (LO != P.LiveOut.end()) {
                                // errs() << "Found\n";
                                P.LiveOut.erase(LO);
                                P.LiveOut.insert(Val);
                                //                    errs() << __LINE__ << " "
                                //                    << *Val << "\n";
                            }
                        }
                        Found = true;
                        break;
                    }
                }
                assert(Found && "Could not find value");
            }
            clear_vertex(V, BG);
            PhisRemoved++;
        }
    }

    Out << "\"phis_removed\" : " << PhisRemoved << ",\n";
    Out << "\"phi_input\" : " << PhiIn << ",\n";

    // TODO : Assert that the graph is a DAG, else FML
}

static void removeConvert(const Path &P, BoostGraph &BG,
                          const map<string, BasicBlock *> &BlockMap,
                          ofstream &Out) {
    uint64_t Count = 0;
    auto TypeMap = boost::get(&VertexProp::Type, BG);
    for (auto &V : dfsTraverse(BG)) {
        if (TypeMap[V] == CONVERT) {
            auto Parent = getParents(V, BG);
            assert(Parent.size() == 1 &&
                   "Expect 1 operand for type conversion");
            auto Children = getChildren(V, BG);
            for (auto &C : Children) {
                add_edge(*Parent.begin(), C, {REG}, BG);
                clear_vertex(V, BG);
                Count++;
            }
        }
    }

    Out << "\"remove_convert\" : " << Count << ",\n";
}

static void removeCBRChain(Vertex V, BoostGraph &BG) {
    // Get the first predecessor which has more than
    // one out edge.
    Vertex CurrV = V;
    Vertex PrevV = 0;
    while (out_degree(CurrV, BG) < 2 && in_degree(CurrV, BG) == 1) {
        PrevV = CurrV;
        CurrV = *getParents(CurrV, BG).begin();
    }

    assert(PrevV && "The loop should exec at least once");
    remove_edge(CurrV, PrevV, BG);
}

static void separateBranches(const Path &P, BoostGraph &BG,
                             const map<string, BasicBlock *> &BlockMap,
                             ofstream &Out) {
    auto TypeMap = boost::get(&VertexProp::Type, BG);
    set<Instruction *> CBRs;
    uint64_t CCount = 0;
    for (auto &V : dfsTraverse(BG)) {
        if (TypeMap[V] == CBR) {
            // Found a conditional branch
            // follow its predecessors until we find
            // a node whose only predecessor is not
            // this branch chain.
            removeCBRChain(V, BG);
            CCount++;
        }
    }
    Out << "\"remove_cbr\" : " << CCount << ",\n";
}

static void liveInHelper(Path &P, Value *Val) {
    if (auto Ins = dyn_cast<Instruction>(Val)) {
        for (auto OI = Ins->op_begin(), EI = Ins->op_end(); OI != EI; OI++) {
            if (auto OIns = dyn_cast<Instruction>(OI)) {
                if (!isBlockInPath(OIns->getParent()->getName().str(), P)) {
                    P.LiveIn.insert(OIns);
                }
            } else
                liveInHelper(P, *OI);
        }
    } else if (auto CE = dyn_cast<ConstantExpr>(Val)) {
        for (auto OI = CE->op_begin(), EI = CE->op_end(); OI != EI; OI++) {
            assert(!isa<Instruction>(OI) &&
                   "Don't expect operand of ConstExpr to be an Instruction");
            liveInHelper(P, *OI);
        }
    } else if (auto Arg = dyn_cast<Argument>(Val))
        P.LiveIn.insert(Arg);
    else if (auto GV = dyn_cast<GlobalVariable>(Val))
        P.Globals.insert(GV);

    // Constants should just fall through and remain
    // in the trace.
}

static void liveInLiveOut(PostDominatorTree *PostDomTree, AliasAnalysis *AA,
                          Path &P, BoostGraph &BG,
                          map<string, BasicBlock *> &BlockMap, ofstream &Out) {
    map<Instruction *, Vertex> InsVertexMap;
    BGL_FORALL_VERTICES(V, BG, BoostGraph)
    InsVertexMap[BG[V].Inst] = V;

    for (auto &V : dfsTraverse(BG)) {
        // Skip the root
        if (BG[V].Type == BB_START)
            continue;

        // PHI Node inputs will be resolved by removePHI
        // added to the live ins and then the node will be
        // removed.
        if (BG[V].Type == PHI)
            continue;

        auto Ins = BG[V].Inst;

        liveInHelper(P, Ins);

        if (dyn_cast<LoadInst>(Ins)) {
            P.MemIn.insert(Ins);
            // Value loaded from memory and used outside path means that this
            // path does not
            // modify the value, this implies that the value can be loaded again
            // so we do
            // not consider it to be a live out.
            continue;

        } else if (dyn_cast<StoreInst>(Ins)) {
            P.MemOut.insert(Ins);
            continue;
        }

        // The path blocks will always be in the BlockMap since they are from
        // the
        // same function, so not checking for membership here.
        // [1] If the live out is post-dominated by the last block, then the
        // value
        // is killed and we don't count it as a true live-out.
        // [2] If the live out is the result of GEP, then don't consider as live
        // out because a) doesn't make sense since address space different for
        // acc/host
        // b) the users outside can path can generate their own addresses.
        auto LastBlock = BlockMap[*P.Seq.rbegin()];
        for (auto UI = Ins->use_begin(), UE = Ins->use_end(); UI != UE; UI++) {
            if (auto UIns = dyn_cast<Instruction>(UI->getUser())) {
                if (!isBlockInPath(UIns->getParent()->getName().str(), P)) {
                    if (!PostDomTree->dominates(LastBlock,
                                                UIns->getParent()) && // [1]
                        !isa<GetElementPtrInst>(Ins)) {               // [2]
                        P.LiveOut.insert(Ins);
                        //                        errs() << __LINE__ << " " <<
                        //                        *Ins << "\n";
                    }
                } else {
                    if (auto Phi = dyn_cast<PHINode>(Ins)) {
                        auto distance =
                            [](vector<string> &Seq, string S) -> uint32_t {
                                uint32_t I = 0;
                                for (auto RI = Seq.rbegin(), RE = Seq.rend();
                                     RI != RE; RI++) {
                                    if (*RI == S)
                                        return I;
                                    I++;
                                }
                                assert(false && "Unreachable");
                            };
                        // Find the topo position of Tgt
                        // If it is topologically before the phi def, then it is
                        // a use
                        // across a backedge which we must add as live-out
                        auto *Tgt = UIns->getParent();
                        auto PosTgt = distance(P.Seq, Tgt->getName().str());
                        auto PosPhi =
                            distance(P.Seq, Phi->getParent()->getName().str());
                        if (PosTgt > PosPhi) {
                            P.LiveOut.insert(Phi);
                        }
                    }
                }
            }
        }
    }

    auto *LastT = BlockMap[*P.Seq.rbegin()]->getTerminator();
    if (auto *RT = dyn_cast<ReturnInst>(LastT)) {
        if (auto *Val = RT->getReturnValue()) {
            P.LiveOut.insert(Val);
            //            errs() << __LINE__ << " " << *Val << "\n";
        }
    }

    Out << "\"live_in\" : " << P.LiveIn.size() << ",\n";
    Out << "\"live_out\" : " << P.LiveOut.size() << ",\n";
    Out << "\"mem_live_in\" : " << P.MemIn.size() << ",\n";
    Out << "\"mem_live_out\" : " << P.MemOut.size() << ",\n";

    // Use alias analysis to reduce mem_live_in / mem_live_out count
    vector<AliasAnalysis::Location> LiveInLocations(P.MemIn.size());
    vector<AliasAnalysis::Location> LiveOutLocations(P.MemOut.size());

    // Too bad we don't have polymorphic lambdas in c++11
    auto ToLocation = [&AA](const Value *Ins) -> AliasAnalysis::Location {
        if (auto LI = dyn_cast<LoadInst>(Ins))
            return AA->getLocation(LI);
        if (auto SI = dyn_cast<StoreInst>(Ins))
            return AA->getLocation(SI);
        assert(false && "Unreachable");
    };

    transform(P.MemIn.begin(), P.MemIn.end(), LiveInLocations.begin(),
              ToLocation);
    transform(P.MemOut.begin(), P.MemOut.end(), LiveOutLocations.begin(),
              ToLocation);

    auto uniqueLocationCount =
        [&AA](vector<AliasAnalysis::Location> &Locs) -> uint32_t {
            vector<AliasAnalysis::Location> UniqueLocations;
            for (auto &L : Locs) {
                bool Found = false;
                for (auto &M : UniqueLocations) {
                    if (AA->isMustAlias(L, M)) {
                        Found = true;
                        break;
                    }
                }
                if (!Found)
                    UniqueLocations.push_back(L);
            }
            return UniqueLocations.size();
        };

    Out << "\"u_mem_live_in\" : " << uniqueLocationCount(LiveInLocations)
        << ",\n";
    Out << "\"u_mem_live_out\" : " << uniqueLocationCount(LiveOutLocations)
        << ",\n";
}

void labelGraphLevel(BoostGraph &BG) {
    auto LevelMap = boost::get(&VertexProp::Level, BG);
    auto AllVertices = dfsTraverse(BG);

    set<Vertex> Seen, Unseen(AllVertices.begin(), AllVertices.end() - 1);
    Seen.insert(static_cast<Vertex>(0));
    map<Vertex, uint32_t> VCount;

    // Initialize the map of counts. Each count is = in_degree
    // of the vertex and initialized to -1. This will be updated
    // per update and when equals in_degree, the vertex will
    // move from unseen to seen.
    for (auto &U : Unseen)
        VCount.emplace(U, 0);

    set<Vertex> Move;
    Move.insert(static_cast<Vertex>(0));
    uint32_t Level = 1;
    while (Unseen.size()) {
        set<Vertex> ToMove;
        for (auto &M : Move) {
            auto Edges = out_edges(M, BG);
            for (auto EI = Edges.first, EE = Edges.second; EI != EE; ++EI) {
                auto C = target(*EI, BG);
                LevelMap[C] = LevelMap[C] > Level ? LevelMap[C] : Level;
                VCount[C]++;

                if (VCount[C] == in_degree(C, BG)) {
                    Unseen.erase(C);
                    ToMove.insert(C);
                }
            }
        }
        assert(ToMove.size() && "Nothing to move");
        Move.clear();
        ToMove.swap(Move);
        Level++;
    }
}

void analyseExecUnit(const Path &P, BoostGraph &BG,
                     const map<string, BasicBlock *> &BlockMap, ofstream &Out) {
    // Execution Units
    // Gather the number of exec units required per level
    // for histogram to figure out how much
    // op-level parallelism is there.

    map<uint32_t, uint32_t> IntMap;
    map<uint32_t, uint32_t> FPMap;
    map<uint32_t, uint32_t> GEPMap;

    uint32_t IntCounter = 0, FPCounter = 0, GEPCounter = 0;

    for (auto &V : dfsTraverse(BG)) {
        // Skip the root and tail vertex
        if (BG[V].Type == BB_START)
            continue;

        auto Level = BG[V].Level;
        switch (BG[V].Type) {
        case VertexType::INT:
            IntMap[Level] += 1;
            IntCounter++;
            break;
        case VertexType::FP:
            FPMap[Level] += 1;
            FPCounter++;
            break;
        case VertexType::GEP:
            GEPMap[Level] += 1;
            GEPCounter++;
            break;
        default:
            break;
        }
    }

    auto mapMax = [](map<uint32_t, uint32_t> &M) -> uint32_t {
        auto K = max_element(M.begin(), M.end(),
                             [](const pair<uint32_t, uint32_t> &P,
                                const pair<uint32_t, uint32_t> &Q) {
                                 return P.second < Q.second;
                             });
        return K->second;
    };

    Out << "\"int_count\" : " << IntCounter << ",\n";
    Out << "\"int_max\" : " << mapMax(IntMap) << ",\n";
    Out << "\"int_exec\" : [";
    bool First = true;
    for (auto &KV : IntMap) {
        if (!First)
            Out << ",";
        Out << "{\"k\":" << KV.first << ",\"v\":" << KV.second << "}";
        First = false;
    }
    Out << "],\n";

    Out << "\"fp_count\" : " << FPCounter << ",\n";
    Out << "\"fp_max\" : " << mapMax(FPMap) << ",\n";
    Out << "\"fp_exec\" : [";
    First = true;
    for (auto &KV : FPMap) {
        if (!First)
            Out << ",";
        Out << "{\"k\":" << KV.first << ",\"v\":" << KV.second << "}";
        First = false;
    }
    Out << "],\n";

    Out << "\"gep_count\" : " << GEPCounter << ",\n";
    Out << "\"gep_max\" : " << mapMax(GEPMap) << ",\n";
    Out << "\"gep_exec\" : [";
    First = true;
    for (auto &KV : GEPMap) {
        if (!First)
            Out << ",";
        Out << "{\"k\":" << KV.first << ",\"v\":" << KV.second << "}";
        First = false;
    }
    Out << "],\n";
}

void analyseSpatialSlack(const Path &P, BoostGraph &BG,
                         const map<string, BasicBlock *> &BlockMap,
                         ofstream &Out) {
    // Spatial Slack -- For each individual operations,
    // find the consumers and the level they belong to.
    // For consumers outside the path, set to Inf.

    auto InsMap = boost::get(&VertexProp::Inst, BG);
    map<Instruction *, Vertex> InsVertexMap;
    BGL_FORALL_VERTICES(V, BG, BoostGraph)
    InsVertexMap[InsMap[V]] = V;

    map<Vertex, uint32_t> SSlack;

    for (auto &V : dfsTraverse(BG)) {
        // Skip the root
        if (BG[V].Type == BB_START)
            continue;
        auto Ins = InsMap[V];
        uint32_t Slack = 0;
        for (auto UI = Ins->use_begin(), UE = Ins->use_end(); UI != UE; UI++) {
            if (auto UIns = dyn_cast<Instruction>(UI->getUser())) {
                if (isBlockInPath(UIns->getParent()->getName().str(), P)) {
                    auto S = BG[InsVertexMap[UIns]].Level - BG[V].Level;
                    Slack = Slack > S ? Slack : S;
                } else {
                    // Slack is inf, value is LiveOut
                    Slack = UINT32_MAX;
                }
            }
        }
        SSlack[V] = Slack;
    }

    Out << "\"s_slack\" : [";
    bool First = true;
    for (auto &KV : SSlack) {
        if (!First)
            Out << ",";
        Out << "{\"k\":" << KV.first << ",\"v\":" << KV.second << "}";
        First = false;
    }
    Out << "],\n";
}

template <typename TGraph>
void cleanGraphHelper(typename TGraph::vertex_descriptor V, const TGraph &G,
                      TGraph &DAG,
                      set<typename TGraph::vertex_descriptor> &Seen,
                      map<typename TGraph::vertex_descriptor,
                          typename TGraph::vertex_descriptor> &VMap,
                      map<typename TGraph::vertex_descriptor,
                          typename TGraph::vertex_descriptor> &WMap) {
    Seen.insert(V);
    auto EI = out_edges(V, G);
    for (auto EBegin = EI.first, EEnd = EI.second; EBegin != EEnd; EBegin++) {
        if (!Seen.count(target(*EBegin, G))) {
            auto W = add_vertex(DAG);
            VMap.emplace(target(*EBegin, G), W);
            WMap.emplace(W, target(*EBegin, G));
            cleanGraphHelper(target(*EBegin, G), G, DAG, Seen, VMap, WMap);
        }
        add_edge(VMap[V], VMap[target(*EBegin, G)], DAG);
    }
}

template <typename TGraph>
void cleanGraph(const TGraph &G, TGraph &DAG,
                map<typename TGraph::vertex_descriptor,
                    typename TGraph::vertex_descriptor> &VMap,
                map<typename TGraph::vertex_descriptor,
                    typename TGraph::vertex_descriptor> &WMap) {
    set<typename TGraph::vertex_descriptor> Seen;
    auto W = add_vertex(DAG);
    VMap.emplace(0, W);
    WMap.emplace(W, 0);
    cleanGraphHelper<TGraph>(0, G, DAG, Seen, VMap, WMap);
}

void writeGraphClust(BoostGraph &G, const Path &P) {
    BoostGraph DAG;
    typedef typename BoostGraph::vertex_descriptor Vertex;
    map<Vertex, Vertex> VMap;
    map<Vertex, Vertex> WMap;

    // Because the bimap did not work
    cleanGraph<BoostGraph>(G, DAG, VMap, WMap);

    // Print out the path graph in graphclust format to
    // be used later for path similarity analysis
    ofstream gcGraph(P.Id + string(".gc"));
    gcGraph << "#" << P.Id << "\n";

    gcGraph << num_vertices(DAG) << "\n";

    BGL_FORALL_VERTICES(V, DAG, BoostGraph)
    gcGraph << VertexTypeStr[G[WMap[V]].Type] << "\n";

    gcGraph << num_edges(DAG) << "\n";

    BGL_FORALL_EDGES(E, DAG, BoostGraph)
    gcGraph << source(E, DAG) << " " << target(E, DAG) << "\n";
    gcGraph << "\n";
    gcGraph.close();
}

// template <typename TGraph>
vector<vector<uint64_t>> decomposeHelper(const BoostGraph &G) {
    // Distill G and then call the decomposition
    // methods, finally remap to original vertices.

    BoostGraph DAG;
    typedef typename BoostGraph::vertex_descriptor Vertex;
    map<Vertex, Vertex> VMap;
    map<Vertex, Vertex> WMap;

    // Because the bimap did not work
    cleanGraph<BoostGraph>(G, DAG, VMap, WMap);

    // Print out the path graph in graphclust format to
    // be used later for path similarity analysis
    // ofstream gcGraph(P.Id+string(".gc"));
    // gcGraph << "#" << P.Id << "\n";
    // BGL_FORALL_VERTICES(V, DAG, BoostGraph)
    // gcGraph << VertexTypeStr[G[WMap[V]].Type] << "\n";
    // gcGraph << num_edges(DAG);
    // BGL_FORALL_EDGES(E, DAG, BoostGraph)
    // gcGraph << source(E, DAG) << " " << target(E, DAG) << "\n";
    // gcGraph << "\n";
    // gcGraph.close();

    // Check whether the graph can be decomposed,
    // our construction as a DAG should ensure this is
    // possible.
    assert(boost::is_bipartite(DAG));

    dilworth::DilworthDecompose<BoostGraph> DD;
    auto Chains = DD.getMinimalChains(DAG);

    // Remap to original vertices
    for (auto &C : Chains)
        for (unsigned I = 0; I < C.size(); I++) {
            assert(WMap.count(C[I]) && "Reverse map does not contain vertex");
            C[I] = WMap[C[I]];
        }

    return Chains;
}

void analyseChains(const Path &P, BoostGraph &BG,
                   const map<string, BasicBlock *> &BlockMap,
                   vector<vector<uint64_t>> &Chains, ofstream &Out) {
    auto printChain = [&BG, &Out](const vector<uint64_t> &Chain) {
        Out << "{ \"level\" : " << BG[Chain[0]].Level << " , \"nodes\" : [";
        bool First = true;
        for (auto &N : Chain) {
            if (!First)
                Out << ",";
            Out << N;
            First = false;
        }
        Out << "]}";
    };

    Out << "\"num_chains\": " << Chains.size() << ",\n";
    double SumChainLength = 0;

    Out << "\"chains\" : [ ";

    bool First = true;
    uint64_t SumChainStores = 0;
    for (auto &C : Chains) {
        if (!First)
            Out << ",";
        SumChainLength += C.size();
        printChain(C);
        for (auto &N : C) {
            if (N == 0)
                continue;
            if (dyn_cast<StoreInst>(BG[N].Inst))
                SumChainStores++;
        }
        First = false;
    }
    Out << "],\n";
    Out << "\"avg_chain_length\" : " << SumChainLength / double(Chains.size())
        << ",\n";
    Out << "\"avg_store_per_chain\" : "
        << SumChainStores / double(Chains.size()) << ",\n";
}

void analyseGeneral(const Path &P, BoostGraph &BG,
                    const map<string, BasicBlock *> &BlockMap, ofstream &Out) {
    uint32_t AllocCounter = 0;
    uint32_t LoadCounter = 0;
    uint32_t StoreCounter = 0;
    uint32_t IndirectCallCount = 0;
    set<string> CalledFunctions;
    for (auto &V : dfsTraverse(BG)) {
        if (BG[V].Type == FUNC) {
            CallSite CS(BG[V].Inst);
            int S = 0;
            Function *F = CS.getCalledFunction();
            if (F) {
                char *Ptr = abi::__cxa_demangle(F->getName().data(), 0, 0, &S);
                CalledFunctions.insert(F->getName().str());
                if (Ptr) {
                    string Name(Ptr);
                    if ((Name.find("malloc") & Name.find("calloc") &
                         Name.find("operator new") & Name.find("alloca")) !=
                        string::npos)
                        AllocCounter++;
                    free(Ptr);
                }
            } else
                IndirectCallCount++;
        }
        if (BG[V].Type == MEM) {
            if (dyn_cast<LoadInst>(BG[V].Inst)) {
                BG[V].Type = MEM_LD;
                LoadCounter++;
            }
            if (dyn_cast<StoreInst>(BG[V].Inst)) {
                BG[V].Type = MEM_ST;
                StoreCounter++;
            }
        }
    }

    // TODO : Alias Analysis
    Out << "\"load_count\" : " << LoadCounter << ",\n";
    Out << "\"store_count\" : " << StoreCounter << ",\n";
    Out << "\"alloc_count\" : " << AllocCounter << ",\n";
    Out << "\"indirect_count\" : " << IndirectCallCount << ",\n";
    Out << "\"num_called_funcs\" : " << CalledFunctions.size() << ",\n";
    Out << "\"called_funcs\" : [";
    bool First = true;
    for (auto &F : CalledFunctions) {
        if (!First)
            Out << ",";
        Out << "\"" << F << "\"";
        First = false;
    }
    Out << "],\n";
}

void analyseTemporalSlack(const Path &P, BoostGraph &BG,
                          const map<string, BasicBlock *> &BlockMap,
                          vector<vector<uint64_t>> &Chains, ofstream &Out) {
    Out << "\"chain_deps\" : [";
    bool First = true;
    uint32_t AvgDepSize = 0;
    for (auto &C : Chains) {
        uint32_t DepChainSize = 0;
        for (uint32_t I = 0; I < C.size(); I++) {
            if (C[I] == 0)
                continue;
            if (dyn_cast<LoadInst>(BG[C[I]].Inst)) {
                DepChainSize = C.size() - I - 1;
                break;
            }
        }

        if (!First)
            Out << ",";
        Out << DepChainSize;
        First = false;
        AvgDepSize += DepChainSize;
    }
    Out << "],\n";
    Out << "\"avg_chain_deps\" : " << double(AvgDepSize) / Chains.size()
        << ",\n";
}

void traceHelper(Function *TraceFunc, Function *GuardFunc,
                 GlobalVariable *LiveOutArray, Path &P, BoostGraph &BG,
                 LLVMContext &Context, map<string, BasicBlock *> &BlockMap) {
    uint64_t MaxLevel = 0;
    BGL_FORALL_VERTICES(V, BG, BoostGraph)
    MaxLevel = MaxLevel < BG[V].Level ? BG[V].Level : MaxLevel;

    auto *TraceBlock = BasicBlock::Create(Context, "entry", TraceFunc, nullptr);

    map<Value *, Value *> OrigToTraceMap;
    map<Value *, Value *> FunctionMap;

    auto handleCallSites =
        [&FunctionMap, &TraceFunc](CallSite &OrigCS, CallSite &TraceCS) {
            assert(OrigCS.getCalledFunction() &&
                   "We do not support indirect function calls in traces.");
            auto *FTy = OrigCS.getCalledFunction()->getFunctionType();
            auto *Val = OrigCS.getCalledValue();
            auto Name = OrigCS.getCalledFunction()->getName();
            if (FunctionMap.count(Val) == 0) {
                Function *ExtFunc =
                    Function::Create(FTy, GlobalValue::ExternalLinkage, Name,
                                     TraceFunc->getParent());
                FunctionMap[Val] = static_cast<Value *>(ExtFunc);
            }
            TraceCS.setCalledFunction(FunctionMap[Val]);
        };

    auto insertGuardCall =
        [&TraceBlock, &GuardFunc, &OrigToTraceMap](Value *Arg) {
            // Value * Arg = CBR->getCondition();
            // There may be a case where a conditional branch inside
            // a path is dependent on a value outside the path; in this
            // case we don't serialize the guard since it can be hoisted
            // above the trace. Ideally, the else of this condition should
            // collect these instances and implement pre-trace guard check.
            // Eg. path 14580 in swaptions.
            if (OrigToTraceMap.count(Arg)) {
                auto CI = CallInst::Create(GuardFunc, {Arg}, "", TraceBlock);
                // Add a ReadNone+NoInline attribute to the CallSite, which
                // will hopefully help the optimizer.
                CI->setDoesNotAccessMemory();
                CI->setIsNoInline();
            }
        };

    auto insertSWGuardCall =
        [&insertGuardCall, &TraceBlock](SwitchInst *SWI, BasicBlock *NB) {
            // Get the value corresponding to the next block
            if (auto *CI = SWI->findCaseDest(NB)) {
                auto *V = SWI->getCondition();
                auto *Cmp = new ICmpInst(
                    *TraceBlock, CmpInst::Predicate::ICMP_EQ, V, CI, "");
                insertGuardCall(Cmp);
            } else {
                // FIXME : Lowering of switch statement to guard calls for
                // default case and for cases with non-unique targets
                // sjeng : gen : 130982547345851
            }
        };

    // Main processing loop; clones instructions from graph
    // into the new trace basic block.
    // All removed vertices are marked with Level = 0
    // so iterating from Level == 1 to MaxLevel will give
    // us only connected vertices.
    for (uint64_t L = 1; L <= MaxLevel; L++) {
        BGL_FORALL_VERTICES(V, BG, BoostGraph) {
            if (L == BG[V].Level) {
                auto OrigIns = BG[V].Inst;

                if (auto CBR = dyn_cast<BranchInst>(OrigIns)) {
                    assert(CBR->isConditional() &&
                           "There should only be conditional branches in here");
                    insertGuardCall(CBR->getCondition());
                    continue;
                } else if (auto SWI = dyn_cast<SwitchInst>(OrigIns)) {
                    string ParentName = SWI->getParent()->getName().str();
                    auto NextBlockName =
                        next(find(P.Seq.begin(), P.Seq.end(), ParentName));
                    assert(NextBlockName != P.Seq.end() &&
                           "Not expecting switchins in last block");
                    auto *NB = BlockMap[*NextBlockName];
                    assert(NB && "Should not be null");
                    insertSWGuardCall(SWI, NB);
                    continue;
                } else if (isa<UnreachableInst>(OrigIns)) {
                    continue;
                }

                auto Ins = OrigIns->clone();

                OrigToTraceMap[dyn_cast<Value>(OrigIns)] = dyn_cast<Value>(Ins);
                TraceBlock->getInstList().push_back(Ins);

                // errs() << V << " BG:" << *(BG[V].Inst) << "\n";
                // errs() << V << " TR:" << *Ins << "\n";

                // If it is a CallSite then create external call
                // declaration for it in the module.
                CallSite CS(OrigIns), TraceCS(Ins);
                if (CS.isCall() || CS.isInvoke())
                    handleCallSites(CS, TraceCS);
            }
        }
    }

    // Add Globals
    for (auto Val : P.Globals) {
        auto OldGV = dyn_cast<GlobalVariable>(Val);
        assert(OldGV && "Could not reconvert to Global Variable");
        GlobalVariable *GV = new GlobalVariable(
            *TraceFunc->getParent(), OldGV->getType()->getElementType(),
            OldGV->isConstant(), GlobalValue::ExternalLinkage,
            (Constant *)nullptr, OldGV->getName(), (GlobalVariable *)nullptr,
            OldGV->getThreadLocalMode(), OldGV->getType()->getAddressSpace());
        GV->copyAttributesFrom(OldGV);
        OrigToTraceMap[OldGV] = GV;
    }

    // Add store instructions to write live-outs to
    // the char array.
    assert(P.LiveOut.size() < INT32_MAX && "Very large number of live outs");

    const DataLayout *DL = TraceFunc->getParent()->getDataLayout();
    int32_t OutIndex = 0;
    auto Int32Ty = IntegerType::getInt32Ty(Context);
    ConstantInt *Zero = ConstantInt::get(Int32Ty, 0);
    for (auto LO : P.LiveOut) {
        // errs() << "LO: " << *LO << "\n";
        // assert(P.LiveIn.count(LO) == 0 && "LOLWAT");
        if (P.LiveIn.count(LO)) {
            // Amazing corner case where a LiveIn (Phi promoted to arg) is
            // propagated through the path to become a LiveOut at the end.
        }
        ConstantInt *Index = ConstantInt::get(Int32Ty, OutIndex);
        Value *GEPIndices[] = {Zero, Index};
        auto *GEP = GetElementPtrInst::Create(LiveOutArray, GEPIndices, "idx",
                                              TraceBlock);
        BitCastInst *BC = new BitCastInst(
            GEP, PointerType::get(LO->getType(), 0), "cast", TraceBlock);
        new StoreInst(LO, BC, false, TraceBlock);
        OutIndex += DL->getTypeStoreSize(LO->getType());
    }

    // Patch instructions to arguments,
    Function::arg_iterator AI = TraceFunc->arg_begin();
    Value *RewriteVal = nullptr;

    for (auto Val : P.LiveIn) {
        RewriteVal = AI++;
        std::vector<User *> Users(Val->user_begin(), Val->user_end());
        for (std::vector<User *>::iterator use = Users.begin(),
                                           useE = Users.end();
             use != useE; ++use) {
            if (Instruction *inst = dyn_cast<Instruction>(*use)) {
                if (inst->getParent()->getParent() == TraceFunc) {
                    inst->replaceUsesOfWith(Val, RewriteVal);
                }
                // if(OrigToTraceMap.count(dyn_cast<Value>(inst))) {
                // dyn_cast<Instruction>(OrigToTraceMap[inst])->replaceUsesOfWith(Val,
                // RewriteVal);

                //}
            }
        }
    }

    // Assign names, if you don't have a name,
    // a name will be assigned to you.
    AI = TraceFunc->arg_begin();
    uint32_t VReg = 0;
    for (auto Val : P.LiveIn) {
        auto Name = Val->getName();
        if (Name.empty())
            AI->setName(StringRef(string("vr.") + to_string(VReg++)));
        else
            AI->setName(Name + string(".in"));
        ++AI;
    }

    // Patch uses -- remap all operands from
    // the original module to new instructions inserted
    // in the trace module.
    for (auto &BB : *TraceFunc) {
        for (auto &Ins : BB) {
            for (auto OI = Ins.op_begin(), OE = Ins.op_end(); OI != OE; ++OI) {
                Value *Val = *OI;
                auto Iter = OrigToTraceMap.find(Val);
                if (Iter != OrigToTraceMap.end())
                    *OI = Iter->second;

                // Reasons why Val won't be in this map, it's a
                // a) constant b) argument c) basic block (for brinst)
                // d) function
            }
        }
    }

    ReturnInst::Create(Context, TraceBlock);
}

void makeTraceGraph(Function *F, BoostGraph &BG) {
    assert(num_vertices(BG) == 0 && "Start with an empty graph");

    Vertex CurrV, BBStart;
    BBStart = boost::add_vertex(
        {0, BB_START, nullptr, VertexTypeStr[BB_START], 0}, BG);
    bm_type VertexInsMap;

    for (auto &BB : *F) {
        for (auto &Ins : BB) {
            if (isa<ReturnInst>(&Ins))
                continue;
            auto Type = getOpType(Ins);
            CurrV =
                boost::add_vertex({0, Type, &Ins, VertexTypeStr[Type], 0}, BG);
            VertexInsMap.insert(bm_index(CurrV, &Ins));
        }
    }

    for (auto &BB : *F) {
        for (auto &Ins : BB) {
            if (isa<ReturnInst>(&Ins))
                continue;
            auto IS = VertexInsMap.right.find(&Ins);
            assert(IS != VertexInsMap.right.end() && "Not found in BiMap");
            auto CurrV = IS->second;

            bool DepsFound = false;
            for (auto OI = Ins.op_begin(), E = Ins.op_end(); OI != E; ++OI) {
                if (auto II = dyn_cast<Instruction>(OI)) {
                    auto IT = VertexInsMap.right.find(II);
                    if (IT != VertexInsMap.right.end()) {
                        DepsFound = true;
                        auto Vx = IT->second;
                        boost::add_edge(Vx, CurrV, {REG}, BG);
                    }
                }
            }

            if (!DepsFound) {
                // Should assert that it is a constant or an argument
                add_edge(BBStart, CurrV, {REG}, BG);
            }
        }
    }
}

static string getChainStr(const vector<Vertex> &C, const BoostGraph &BG) {
    string retStr;
    bool First = true;
    for (auto V : C) {
        if (BG[V].Type == BB_START)
            continue;
        if (!First)
            retStr += ",";
        retStr += VertexTypeStr[BG[V].Type];
        First = false;
    }
    return retStr;
}

static void toChainGraphHelper(const vector<vector<Vertex>> &Chains,
                               const BoostGraph &BG, BoostGraph &CG) {

    map<Vertex, uint32_t> VertexToChainMap;
    map<uint32_t, set<Vertex>> ChainInputs;
    vector<Vertex> ChainMap(Chains.size());
    DEBUG(errs() << "NumChains : " << Chains.size() << "\n");
    for (uint32_t I = 0; I < Chains.size(); I++) {
        auto &C = Chains[I];
        auto CV = boost::graph_traits<BoostGraph>::null_vertex();
        if (BG[C[0]].Type == BB_START)
            CV = add_vertex({0, BB_START, nullptr, "BB_START", 0}, CG);
        else
            CV = add_vertex({0, CHAIN, nullptr, getChainStr(C, BG), 0}, CG);
        ChainMap[I] = CV;
        DEBUG(errs() << "[" << I << "] ");
        ChainInputs[I] = set<Vertex>();
        for (auto &V : C) {
            DEBUG(errs() << V << " ");
            VertexToChainMap[V] = I;
            auto EI = in_edges(V, BG);
            DEBUG(errs() << "(");
            for (auto ES = EI.first, EE = EI.second; ES != EE; ES++) {
                auto S = source(*ES, BG);
                ChainInputs[I].insert(S);
                DEBUG(errs() << S << " ");
            }
            DEBUG(errs() << ") ");
        }
        DEBUG(errs() << "\n");
    }
    DEBUG(errs() << "-------------\n");

    for (auto &VS : ChainInputs) {
        auto CN = ChainMap[VS.first];
        for (auto V : VS.second) {
            // Get the corresponding chain node
            // for the given input.
            auto ChainId = VertexToChainMap[V];
            auto CS = ChainMap[ChainId];
            // Add an edge if there exists none
            if (!edge(CS, CN, CG).second && CS != CN)
                add_edge(CS, CN, {REG}, CG);
        }
    }
}

// static void
// toSCCGraph(const BoostGraph& BG, BoostGraph& CG) {
// auto Chains = decomposeHelper(BG);

//// Only remove the BB_START vertex

// assert(BG[Chains[0][0]].Type == BB_START &&
//"Need to find BB_START");

// Chains.push_back(vector<Vertex>(Chains[0].begin()+1, Chains[0].end()));
// Chains[0].erase(Chains[0].begin()+1, Chains[0].end());

// toChainGraphHelper(Chains, BG, CG);

//// Collapse the SCC components into a single
//// node and account for it in terms of area
//// only.

//// Since we use listS for the VertexList, we don't have
//// an internal vertex_index property to grab and use.
//// Thus we construct a vertex_index map to use.
// typedef map<Vertex, uint32_t> IndexMap;
// IndexMap IM;
// boost::associative_property_map<IndexMap> APM(IM);
// int i=0;
// BGL_FORALL_VERTICES(V, BG, BoostGraph) {
// put(APM, V, i++);
//}

// std::vector<int> Component(num_vertices(CG));
// int NumComponents = strong_components(CG,
// make_iterator_property_map(Component.begin(), APM));

// DEBUG(errs() << "NumComponents "<< NumComponents << "\n");
// DEBUG(errs() << "NumVertices "<< num_vertices(CG) << "\n");

// for(uint32_t I = 0; I < Component.size(); I++) {
// DEBUG(errs() << "[" << IM[I] << "] "
//<< Component[I] << "\n");
//}

// assert(NumComponents == num_vertices(CG));

//}

static void toChainGraph(const BoostGraph &BG, BoostGraph &CG) {
    auto Chains = decomposeHelper(BG);

    assert(num_vertices(CG) == 0 &&
           "Need an empty graph to construct a chain graph");

    // Break chains at side-outs
    bool Changed = false;
    do {
        Changed = false;
        uint32_t I = 0;
        uint32_t J = 0;
        for (; J < Chains.size(); J++) {
            auto &C = Chains[J];
            // We don't need to check the last vertex, so stop 1
            // previous to that.
            for (I = 0; I < C.size() - 1; I++) {
                auto EI = out_edges(C[I], BG);
                for (auto EB = EI.first, EE = EI.second; EB != EE; EB++) {
                    auto T = target(*EB, BG);
                    if (find(C.begin(), C.end(), T) == C.end()) {
                        Changed = true;
                        break;
                    }
                }
                if (Changed)
                    break;
                // Else get all the targets of the out_edges
                // and check if any are outside this chain.
                // If yes, then break chain.
            }
            if (Changed)
                break;
        }
        if (Changed) {
            Chains.push_back(
                vector<Vertex>(Chains[J].begin() + I + 1, Chains[J].end()));
            Chains[J].erase(Chains[J].begin() + I + 1, Chains[J].end());
        }
    } while (Changed);

    toChainGraphHelper(Chains, BG, CG);
}

static void generateChainsTxt(const BoostGraph &BG) {
    auto Chains = decomposeHelper(BG);
    // Write out chains to a file, we will
    // use this for mining frequent chains later
    std::ofstream ChainFile("chains.txt", ios::app);
    assert(ChainFile.is_open() && "Could not open chains.txt");

    for (auto &C : Chains) {
        bool First = true;
        for (auto &V : C) {
            if (BG[V].Type == BB_START)
                continue;
            if (!First)
                ChainFile << ",";
            ChainFile << BG[V].Type;
            First = false;
        }
        ChainFile << "\n";
    }
}

static void generateTraceFromPath(Path &P, BoostGraph &BG,
                                  map<string, BasicBlock *> &BlockMap) {
    auto BB = (*BlockMap.begin()).second;
    auto DataLayoutStr = BB->getDataLayout();
    auto TargetTripleStr = BB->getParent()->getParent()->getTargetTriple();

    DEBUG(errs() << "Generating Trace " << P.Id << "\n");
    Module *Mod = new Module(P.Id + string("-trace"), getGlobalContext());
    Mod->setDataLayout(DataLayoutStr);
    Mod->setTargetTriple(TargetTripleStr);

    auto VoidTy = Type::getVoidTy(Mod->getContext());

    std::vector<Type *> ParamTy;

    // Add the types of the input values
    // to the function's argument list
    for (auto Val : P.LiveIn)
        ParamTy.push_back(Val->getType());

    FunctionType *TrFuncType = FunctionType::get(VoidTy, ParamTy, false);

    // Create the trace function
    Function *TraceFunc = Function::Create(
        TrFuncType, GlobalValue::ExternalLinkage, "__trace_func", Mod);

    // Create an external function which is used to
    // model all guard checks.
    auto Int1Ty = IntegerType::getInt1Ty(Mod->getContext());
    FunctionType *GuFuncType = FunctionType::get(VoidTy, Int1Ty, false);

    // Create the guard function
    Function *GuardFunc = Function::Create(
        GuFuncType, GlobalValue::ExternalLinkage, "__guard_func", Mod);

    // Create the Output Array as a global variable
    uint32_t LiveOutSize = 0;
    const DataLayout *DL = Mod->getDataLayout();
    for_each(P.LiveOut.begin(), P.LiveOut.end(),
             [&LiveOutSize, &DL](const Value *Val) {
                 LiveOutSize += DL->getTypeStoreSize(Val->getType());
             });
    ArrayType *CharArrTy =
        ArrayType::get(IntegerType::get(Mod->getContext(), 8), LiveOutSize);
    auto *Initializer = ConstantAggregateZero::get(CharArrTy);
    GlobalVariable *LOA =
        new GlobalVariable(*Mod, CharArrTy, false, GlobalValue::CommonLinkage,
                           Initializer, "__live_outs");

    LOA->setAlignment(8);

    traceHelper(TraceFunc, GuardFunc, LOA, P, BG, Mod->getContext(), BlockMap);

    // Strip the debug information from the module, since it
    // may contain invalid references to value which this trace
    // does not use. The optimizer will complain about this.
    StripDebugInfo(*Mod);

    error_code EC;
    string Name = (P.Id) + string(".trace.ll");
    raw_fd_ostream File(Name, EC, sys::fs::OpenFlags::F_RW);
    Mod->print(File, nullptr);
    File.close();

    // Dumbass verifyModule function returns false if no
    // errors are found. Ref "llvm/IR/Verifier.h":46
    assert(!verifyModule(*Mod, &errs()) && "Module verification failed!");

    // error_code EC;
    // string Name = (P.Id) + string(".trace.ll");
    // raw_fd_ostream File(Name, EC, sys::fs::OpenFlags::F_RW);
    // Mod->print(File, nullptr);
    // File.close();

    BoostGraph TG(0);
    makeTraceGraph(TraceFunc, TG);
    labelGraphLevel(TG);
    writeGraph(P, TG, "trace");

    // Do O3 optimizations on the trace here

    PassManagerBuilder PMB;
    PMB.OptLevel = 3;
    PMB.SLPVectorize = false;
    PMB.BBVectorize = false;
    PassManager PM;
    PMB.populateModulePassManager(PM);
    PM.run(*Mod);

    Name = (P.Id) + string(".trace.O3.ll");
    raw_fd_ostream File3(Name, EC, sys::fs::OpenFlags::F_RW);
    Mod->print(File3, nullptr);
    File3.close();

    BoostGraph TGO3(0);
    makeTraceGraph(TraceFunc, TGO3);
    labelGraphLevel(TGO3);
    writeGraph(P, TGO3, "trace.O3");
    generateChainsTxt(TGO3);

    BoostGraph CG(0);
    DEBUG(errs() << "Path: " << P.Id << "\n");

    toChainGraph(TGO3, CG);
    labelGraphLevel(CG);
    writeGraph(P, CG, "chain");

    // BoostGraph SCCG(0);
    // toSCCGraph(TGO3, SCCG);
    // labelGraphLevel(SCCG); // TODO : Implement SCC before enabling
    // writeGraph(P, SCCG, "scc");

    delete Mod;
}

bool stripFunctions(BoostGraph &BG) {
    // Strip out function calls which don't
    // make sense for the trace.
    // a) printf, cout, cerr
    // b) assert
    // Also check for indirect call and return
    // true if it is found. This will terminate
    // trace generation.
    for (auto V : dfsTraverse(BG)) {
        if (BG[V].Type == FUNC) {
            auto CS = CallSite(BG[V].Inst);
            if (auto F = CS.getCalledFunction()) {
                auto Name = F->getName().str();
                // Possible false positives because searching for substrings?
                if ((Name.find("printf") & Name.find("assert") &
                     Name.find("llvm.dbg") & Name.find("llvm.lifetime") &
                     Name.find("gettimeofday")) != string::npos) {
                    // Erase the instruction from the parent
                    // Dead ops which lead to it will automatically
                    // taken care of by the optimizer during trace
                    // generation.
                    BG[V].Inst->eraseFromParent();
                    BG[V].Level = 0;
                    // printf returns the number of chars printed,
                    // but who checks that seriously?
                    errs() << "Stripping out function : " << Name << "\n";
                    clear_vertex(V, BG);
                }
            } else {
                if (out_degree(V, BG) == 0)
                    errs() << "Indirect call with no uses.\n";
                // Found an indirect function call
                return true;
            }
        }
    }
    return false;
}

static inline void bSliceDFSHelper(
    BasicBlock *BB, DenseSet<BasicBlock *> &BSlice,
    DenseSet<pair<const BasicBlock *, const BasicBlock *>> &BackEdges) {
    BSlice.insert(BB);
    for (auto PB = pred_begin(BB), PE = pred_end(BB); PB != PE; PB++) {
        if (!BSlice.count(*PB) && BackEdges.count(make_pair(*PB, BB)) == 0)
            bSliceDFSHelper(*PB, BSlice, BackEdges);
    }
}

static DenseSet<BasicBlock *>
bSliceDFS(BasicBlock *Begin,
          DenseSet<pair<const BasicBlock *, const BasicBlock *>> &BackEdges) {
    DenseSet<BasicBlock *> BSlice;
    bSliceDFSHelper(Begin, BSlice, BackEdges);
    return BSlice;
}

static inline void fSliceDFSHelper(
    BasicBlock *BB, DenseSet<BasicBlock *> &FSlice,
    DenseSet<pair<const BasicBlock *, const BasicBlock *>> &BackEdges) {
    FSlice.insert(BB);
    for (auto SB = succ_begin(BB), SE = succ_end(BB); SB != SE; SB++) {
        if (!FSlice.count(*SB) && BackEdges.count(make_pair(BB, *SB)) == 0)
            fSliceDFSHelper(*SB, FSlice, BackEdges);
    }
}

static DenseSet<BasicBlock *>
fSliceDFS(BasicBlock *Begin,
          DenseSet<pair<const BasicBlock *, const BasicBlock *>> &BackEdges) {
    DenseSet<BasicBlock *> FSlice;
    fSliceDFSHelper(Begin, FSlice, BackEdges);
    return FSlice;
}

static DenseSet<BasicBlock *>
getChop(BasicBlock *StartBB, BasicBlock *LastBB,
        DenseSet<pair<const BasicBlock *, const BasicBlock *>> &BackEdges) {

    DEBUG(errs() << "BackEdges : \n");
    for (auto &BE : BackEdges) {
        DEBUG(errs() << BE.first->getName() << "->" << BE.second->getName()
                     << "\n");
    }
    DEBUG(errs() << "------------------------------\n");

    // Compute Forward Slice from starting of path
    // Compute Backward Slice from last block in path
    // Compute the set intersection (chop) of these two
    // Construct an array ref of the blocks and hand off to the CodeExtractor

    DenseSet<BasicBlock *> FSlice = fSliceDFS(StartBB, BackEdges);
    DenseSet<BasicBlock *> BSlice = bSliceDFS(LastBB, BackEdges);

    DenseSet<BasicBlock *> Chop;
    for (auto &FB : FSlice)
        if (BSlice.count(FB))
            Chop.insert(FB);

    DEBUG(errs() << "Forward : " << StartBB->getName() << "\n");
    for (auto &F : FSlice) {
        DEBUG(errs() << F->getName() << "\n");
    }
    DEBUG(errs() << "------------------------------\n");

    DEBUG(errs() << "Backward : " << LastBB->getName() << "\n");
    for (auto &B : BSlice) {
        DEBUG(errs() << B->getName() << "\n");
    }
    DEBUG(errs() << "------------------------------\n");

    DEBUG(errs() << "Chop : \n");
    for (auto &C : Chop) {
        DEBUG(errs() << C->getName() << "\n");
    }
    DEBUG(errs() << "------------------------------\n");

    return Chop;
}

static void liveInHelperStatic(DenseSet<BasicBlock *> &Chop,
                               SmallVector<Value *, 16> &LiveIn,
                               DenseSet<Value *> &Globals, Value *Val) {
    if (auto Ins = dyn_cast<Instruction>(Val)) {
        for (auto OI = Ins->op_begin(), EI = Ins->op_end(); OI != EI; OI++) {
            if (auto OIns = dyn_cast<Instruction>(OI)) {
                if (!Chop.count(OIns->getParent())) {
                    LiveIn.push_back(OIns);
                }
            } else
                liveInHelperStatic(Chop, LiveIn, Globals, *OI);
        }
    } else if (auto CE = dyn_cast<ConstantExpr>(Val)) {
        for (auto OI = CE->op_begin(), EI = CE->op_end(); OI != EI; OI++) {
            assert(!isa<Instruction>(OI) &&
                   "Don't expect operand of ConstExpr to be an Instruction");
            liveInHelperStatic(Chop, LiveIn, Globals, *OI);
        }
    } else if (auto Arg = dyn_cast<Argument>(Val))
        LiveIn.push_back(Arg);
    else if (auto GV = dyn_cast<GlobalVariable>(Val))
        Globals.insert(GV);

    // Constants should just fall through and remain
    // in the trace.
}

void staticHelper(
    Function *StaticFunc, Function *GuardFunc, GlobalVariable *LOA,
    SmallVector<Value *, 16> &LiveIn, DenseSet<Value *> &LiveOut,
    DenseSet<Value *> &Globals, SmallVector<BasicBlock *, 16> &RevTopoChop,
    DenseSet<pair<const BasicBlock *, const BasicBlock *>> &BackEdges,
    LLVMContext &Context, ofstream &StatsFile) {

    ValueToValueMapTy VMap;

    auto handleCallSites =
        [&VMap, &StaticFunc](CallSite &OrigCS, CallSite &StaticCS) {
            assert(OrigCS.getCalledFunction() &&
                   "We do not support indirect function calls in traces.");
            auto *FTy = OrigCS.getCalledFunction()->getFunctionType();
            auto *Val = OrigCS.getCalledValue();
            auto Name = OrigCS.getCalledFunction()->getName();
            if (VMap.count(Val) == 0) {
                Function *ExtFunc =
                    Function::Create(FTy, GlobalValue::ExternalLinkage, Name,
                                     StaticFunc->getParent());
                assert(VMap.count(Val) == 0 && "Need new values");
                VMap[Val] = static_cast<Value *>(ExtFunc);
            }
            StaticCS.setCalledFunction(VMap[Val]);
        };

    // Add Globals
    for (auto Val : Globals) {
        auto OldGV = dyn_cast<GlobalVariable>(Val);
        assert(OldGV && "Could not reconvert to Global Variable");
        GlobalVariable *GV = new GlobalVariable(
            *StaticFunc->getParent(), OldGV->getType()->getElementType(),
            OldGV->isConstant(), GlobalValue::ExternalLinkage,
            (Constant *)nullptr, OldGV->getName(), (GlobalVariable *)nullptr,
            OldGV->getThreadLocalMode(), OldGV->getType()->getAddressSpace());
        GV->copyAttributesFrom(OldGV);
        assert(VMap.count(OldGV) == 0 && "Need new values");
        VMap[OldGV] = GV;
    }

    for (auto IT = RevTopoChop.rbegin(), IE = RevTopoChop.rend(); IT != IE;
         IT++) {
        auto C = *IT;
        auto *NewBB = BasicBlock::Create(
            Context, StringRef("my_") + C->getName(), StaticFunc, nullptr);
        assert(VMap.count(C) == 0 && "Need new values");
        VMap[C] = NewBB;
        for (auto &I : *C) {
            if (find(LiveIn.begin(), LiveIn.end(), &I) != LiveIn.end())
                continue;
            auto *NewI = I.clone();
            NewBB->getInstList().push_back(NewI);
            //errs() << "Ins : " << *NewI << "\n";
            assert(VMap.count(&I) == 0 && "Need new values");
            VMap[&I] = NewI;
            CallSite CS(&I), TraceCS(NewI);
            if (CS.isCall() || CS.isInvoke())
                handleCallSites(CS, TraceCS);
        }
    }

    // Assign names, if you don't have a name,
    // a name will be assigned to you.
    Function::arg_iterator AI = StaticFunc->arg_begin();
    uint32_t VReg = 0;
    for (auto Val : LiveIn) {
        auto Name = Val->getName();
        if (Name.empty())
            AI->setName(StringRef(string("vr.") + to_string(VReg++)));
        else
            AI->setName(Name + string(".in"));
        // VMap[Val] = AI;
        ++AI;
    }

    auto inChop = [&RevTopoChop](const BasicBlock *BB) -> bool {
        return (find(RevTopoChop.begin(), RevTopoChop.end(), BB) !=
                RevTopoChop.end());
    };

    auto rewriteUses =
        [&VMap, &RevTopoChop, &StaticFunc](Value *Val, Value *RewriteVal) {
            std::vector<User *> Users(Val->user_begin(), Val->user_end());
            for (std::vector<User *>::iterator use = Users.begin(),
                                               useE = Users.end();
                 use != useE; ++use) {
                if (Instruction *inst = dyn_cast<Instruction>(*use)) {
                    if (inst->getParent()->getParent() == StaticFunc) {
                        inst->replaceUsesOfWith(Val, RewriteVal);
                    }
                }
            }
        };

    AI = StaticFunc->arg_begin();
    // Patch instructions to arguments,
    for (auto Val : LiveIn) {
        Value *RewriteVal = AI++;
        rewriteUses(Val, RewriteVal);
    }

    for (auto IT = RevTopoChop.rbegin(), IE = RevTopoChop.rend(); IT != IE;
         IT++) {
        auto *BB = *IT;
        for (auto &I : *BB) {
            // If the original instruction has not been promoted to
            // a live-in, then rewrite it's users if they are in the
            // static function.
            if (find(LiveIn.begin(), LiveIn.end(), &I) != LiveIn.end())
                continue;
            std::vector<User *> Users(I.user_begin(), I.user_end());
            for (std::vector<User *>::iterator use = Users.begin(),
                                               useE = Users.end();
                 use != useE; ++use) {
                if (Instruction *inst = dyn_cast<Instruction>(*use)) {
                    if (inst->getParent()->getParent() == StaticFunc) {
                        assert(VMap[&I] && "Value not found in ValMap");
                        inst->replaceUsesOfWith(&I, VMap[&I]);
                    }
                }
            }
        }
    }

    // Patch Globals Lambda
    function<void(Value&)> handleOperands;
    handleOperands = [&VMap, &handleOperands, &StaticFunc](Value& V) {
        User &I = *cast<User>(&V); 
        for (auto OI = I.op_begin(), E = I.op_end(); OI != E; ++OI) {
            if(auto CE = dyn_cast<ConstantExpr>(*OI)) {
                DEBUG(errs() << V << "\n is a ConstExpr\n");
                handleOperands(*CE);
            }
            if (auto *GV = dyn_cast<GlobalVariable>(*OI)) {
                // Since we may have already patched the global
                // don't try to patch it again.
                if (VMap.count(GV) == 0) continue;
                DEBUG(errs() << " has unpatched global\n");
                // Check if we came from a ConstantExpr
                if(auto CE = dyn_cast<ConstantExpr>(&V)) {
                    int32_t OpIdx = -1;
                    while(I.getOperand(++OpIdx) != GV);
                    auto NCE = CE->getWithOperandReplaced(OpIdx, 
                                         cast<Constant>(VMap[GV]));
                    DEBUG(errs() << "Num Uses: " << CE->getNumUses() << "\n");
                    
                    vector<User *> Users(CE->user_begin(), CE->user_end());
                    for (auto U = Users.begin(),
                              UE = Users.end(); U != UE; ++U) {
                        auto Ins = dyn_cast<Instruction>(*U);
                        if( Ins->getParent()->getParent() == StaticFunc) {
                            Ins->replaceUsesOfWith(CE, NCE);
                            DEBUG(errs() << "Patched: " << *Ins << "\n");
                        }
                    }             
                } else {
                    I.replaceUsesOfWith(GV, VMap[GV]);
                }
            }
        }
    };

    // Patch Globals
    for (auto &BB : *StaticFunc) {
        for (auto &I : BB) {
            handleOperands(I);
        }
    }

    // Patch branches

    auto *BB = cast<BasicBlock>(VMap[RevTopoChop.front()]);
    BB->getTerminator()->eraseFromParent();
    ReturnInst::Create(Context, BB);

    auto insertGuardCall =
        [&GuardFunc, &VMap](const BranchInst *CBR, BasicBlock *Blk) {
            Value *Arg = CBR->getCondition();
            auto CI = CallInst::Create(GuardFunc, {Arg}, "", Blk);
            // Add a ReadNone+NoInline attribute to the CallSite, which
            // will hopefully help the optimizer.
            CI->setDoesNotAccessMemory();
            CI->setIsNoInline();
        };

    BasicBlock *SwitchTgt = nullptr;

    uint64_t GuardChecks = 0;
    for (auto IT = next(RevTopoChop.begin()), IE = RevTopoChop.end(); IT != IE;
         IT++) {
        auto *NewBB = cast<BasicBlock>(VMap[*IT]);
        auto T = NewBB->getTerminator();

        // LLVM 3.7+
        // assert(!isa<CatchEndPadInst>(T) && "Not handled");
        // assert(!isa<CatchPadInst>(T) && "Not handled");
        // assert(!isa<CatchReturnInst>(T) && "Not handled");
        // assert(!isa<CleanupEndPadInst>(T) && "Not handled");
        // assert(!isa<CleanupReturnInst>(T) && "Not handled");
        // assert(!isa<TerminatePadInst>(T) && "Not handled");

        assert(!isa<IndirectBrInst>(T) && "Not handled");
        assert(!isa<InvokeInst>(T) && "Not handled");
        assert(!isa<ResumeInst>(T) && "Not handled");
        assert(!isa<ReturnInst>(T) && "Should not occur");
        assert(!isa<UnreachableInst>(T) && "Should not occur");

        if (auto *SI = dyn_cast<SwitchInst>(T)) {
            // Idea is to create a new basic block which will
            // only contain the guard call with false passed
            // as argument if the target block is not in the
            // chop.
            vector<uint32_t> ToPatch;
            for (uint32_t I = 0; I < SI->getNumSuccessors(); I++) {
                auto Tgt = SI->getSuccessor(I);
                if (!inChop(Tgt)) {
                    ToPatch.push_back(I);
                } else {
                    assert(VMap[Tgt] && "Switch target not found");
                    SI->setSuccessor(I, cast<BasicBlock>(VMap[Tgt]));
                }
            }

            if (ToPatch.size()) {
                // Create a guard block if it does not exist
                // already.
                if (!SwitchTgt) {
                    SwitchTgt = BasicBlock::Create(Context, "switch_guard",
                                                   StaticFunc, nullptr);
                    auto BoolType = IntegerType::getInt1Ty(Context);
                    Value *Arg = ConstantInt::get(BoolType, 0, false);
                    auto CI = CallInst::Create(GuardFunc, {Arg}, "", SwitchTgt);
                    CI->setDoesNotAccessMemory();
                    CI->setIsNoInline();
                    ReturnInst::Create(Context, SwitchTgt);
                }
                for (auto P : ToPatch) {
                    SI->setSuccessor(P, SwitchTgt);
                }
            }
        } else if (auto *BrInst = dyn_cast<BranchInst>(T)) {
            auto NS = T->getNumSuccessors();
            if (NS == 1) {
                // Unconditional branch target *must* exist in chop
                // since otherwith it would not be reachable from the
                // last block in the path.
                auto BJ = T->getSuccessor(0);
                assert(VMap[BJ] && "Value not found in map");
                T->setSuccessor(0, cast<BasicBlock>(VMap[BJ]));
            } else {
                SmallVector<BasicBlock *, 2> Targets;
                for (unsigned I = 0; I < NS; I++) {
                    auto BL = T->getSuccessor(I);
                    if (inChop(BL) &&
                        BackEdges.count(make_pair(*IT, BL)) == 0) {
                        assert(VMap[BL] && "Value not found in map");
                        Targets.push_back(cast<BasicBlock>(VMap[BL]));
                    }
                }

                assert(Targets.size() &&
                       "At least one target should be in the chop");

                // auto *BrInst = dyn_cast<BranchInst>(T);
                if (Targets.size() == 2) {
                    BrInst->setSuccessor(0, cast<BasicBlock>(Targets[0]));
                    BrInst->setSuccessor(1, cast<BasicBlock>(Targets[1]));
                } else {
                    GuardChecks++;
                    insertGuardCall(BrInst, BrInst->getParent());
                    T->eraseFromParent();
                    BranchInst::Create(cast<BasicBlock>(Targets[0]), NewBB);
                }
            }
        } else {
            assert(false && "Unknown TerminatorInst");
        }
    }

    StatsFile << "\"num_guards\" : " << GuardChecks << "\n";

    auto handlePhis = [&VMap, &RevTopoChop, &LiveIn, &BackEdges](PHINode *Phi) {
        auto NV = Phi->getNumIncomingValues();
        vector<BasicBlock *> ToRemove;
        for (unsigned I = 0; I < NV; I++) {
            auto *Blk = Phi->getIncomingBlock(I);
            auto *Val = Phi->getIncomingValue(I);

            // Is this a backedge? Remove the incoming value
            // Is this predicated on a block outside the chop? Remove
            if (BackEdges.count(make_pair(Blk, Phi->getParent())) ||
                find(RevTopoChop.begin(), RevTopoChop.end(), Blk) ==
                    RevTopoChop.end()) {
                ToRemove.push_back(Blk);
                continue;
            }
            assert(VMap[Phi] &&
                   "New Phis should have been added by Instruction clone");
            auto *NewPhi = cast<PHINode>(VMap[Phi]);
            assert(VMap[Blk] && "Value not found in ValMap");
            NewPhi->setIncomingBlock(I, cast<BasicBlock>(VMap[Blk]));

            // Rewrite the value if it is available in the val map
            if (VMap.count(Val)) {
                NewPhi->setIncomingValue(I, VMap[Val]);
            }
        }
        for (auto R : ToRemove) {
            assert(VMap[Phi] &&
                   "New Phis should have been added by Instruction clone");
            auto *NewPhi = cast<PHINode>(VMap[Phi]);
            NewPhi->removeIncomingValue(R, false);
        }
    };

    // Patch the Phis of all the blocks in Topo order
    // apart from the first block (those become inputs)
    for (auto BB = next(RevTopoChop.rbegin()), BE = RevTopoChop.rend();
         BB != BE; BB++) {
        for (auto &Ins : **BB) {
            if (auto *Phi = dyn_cast<PHINode>(&Ins)) {
                handlePhis(Phi);
            }
        }
    }

    // Add store instructions to write live-outs to
    // the char array.
    const DataLayout *DL = StaticFunc->getParent()->getDataLayout();
    int32_t OutIndex = 0;
    auto Int32Ty = IntegerType::getInt32Ty(Context);
    ConstantInt *Zero = ConstantInt::get(Int32Ty, 0);
    for (auto &L : LiveOut) {
        // errs() << "Storing LO: " << *L << "\n";
        // FIXME : Sometimes L may not be in the block map since it could be a
        // self-loop (?)
        // if it is so then don't bother since all instructions are accounted
        // for.
        // EG: primal_net_simplex in mcf2000
        if (Value *LO = VMap[L]) {
            auto *Block = cast<Instruction>(LO)->getParent();
            ConstantInt *Index = ConstantInt::get(Int32Ty, OutIndex);
            Value *GEPIndices[] = {Zero, Index};
            auto *GEP = GetElementPtrInst::Create(LOA, GEPIndices, "idx",
                                                  Block->getTerminator());
            BitCastInst *BC =
                new BitCastInst(GEP, PointerType::get(LO->getType(), 0), "cast",
                                Block->getTerminator());
            new StoreInst(LO, BC, false, Block->getTerminator());
            OutIndex += DL->getTypeStoreSize(LO->getType());
        }
    }
}

static void getTopoChopHelper(
    BasicBlock *BB, DenseSet<BasicBlock *> &Chop, DenseSet<BasicBlock *> &Seen,
    SmallVector<BasicBlock *, 16> &Order,
    DenseSet<pair<const BasicBlock *, const BasicBlock *>> &BackEdges) {
    Seen.insert(BB);
    for (auto SB = succ_begin(BB), SE = succ_end(BB); SB != SE; SB++) {
        if (!Seen.count(*SB) && Chop.count(*SB)) {
            getTopoChopHelper(*SB, Chop, Seen, Order, BackEdges);
        }
    }
    Order.push_back(BB);
}

static SmallVector<BasicBlock *, 16>
getTopoChop(DenseSet<BasicBlock *> &Chop, BasicBlock *StartBB,
            DenseSet<pair<const BasicBlock *, const BasicBlock *>> &BackEdges) {
    SmallVector<BasicBlock *, 16> Order;
    DenseSet<BasicBlock *> Seen;
    getTopoChopHelper(StartBB, Chop, Seen, Order, BackEdges);
    return Order;
}

static inline bool checkIntrinsic(Function *F) {
    auto Name = F->getName();
    if (Name.startswith("llvm.dbg.") ||      // This will be stripped out
        Name.startswith("llvm.lifetime.") || // This will be stripped out
        Name.startswith("llvm.uadd.") ||     // Handled in the Verilog module
        Name.startswith("llvm.umul.") ||     // Handled in the Verilog module
        Name.startswith("llvm.bswap.") ||    // Handled in the Verilog module
        Name.startswith("llvm.fabs."))       // Handled in the Verilog module
        return false;
    else
        return true;
}

static bool verifyChop(const DenseSet<BasicBlock *> Chop) {
    for (auto &CB : Chop) {
        for (auto &I : *CB) {
            CallSite CS(&I);
            if (CS.isCall() || CS.isInvoke()) {
                if (!CS.getCalledFunction()) {
                    return false;
                } else {
                    if (CS.getCalledFunction()->isDeclaration() &&
                        checkIntrinsic(CS.getCalledFunction())) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

static void generateStaticGraphFromPath(const Path &P,
                                        map<string, BasicBlock *> &BlockMap,
                                        PostDominatorTree *PDT,
                                        ofstream &StatsFile) {

    auto *StartBB = BlockMap[P.Seq.front()];
    auto *LastBB = BlockMap[P.Seq.back()];

    SmallVector<std::pair<const BasicBlock *, const BasicBlock *>, 8>
        BackEdgesVec;
    FindFunctionBackedges(*StartBB->getParent(), BackEdgesVec);
    DenseSet<pair<const BasicBlock *, const BasicBlock *>> BackEdges;

    for (auto &BE : BackEdgesVec) {
        BackEdges.insert(BE);
    }

    auto Chop = getChop(StartBB, LastBB, BackEdges);

    StatsFile << "\"num_blocks\" : " << Chop.size() << ",\n";

    DenseSet<const BasicBlock *> ConstChop;
    for (auto &B : Chop)
        ConstChop.insert(B);

    uint32_t NumBackEdges = 0;
    for (auto BE : BackEdges) {
        if (ConstChop.count(BE.first) + ConstChop.count(BE.second) == 2)
            NumBackEdges++;
    }
    StatsFile << "\"backedges\" : " << NumBackEdges << ",\n";

    bool isGood = verifyChop(Chop);
    StatsFile << "\"valid\" : " << (isGood ? "true,\n" : "false,\n");

    auto DataLayoutStr = StartBB->getDataLayout();
    auto TargetTripleStr = StartBB->getParent()->getParent()->getTargetTriple();

    Module *Mod = new Module(P.Id + string("-static"), getGlobalContext());
    Mod->setDataLayout(DataLayoutStr);
    Mod->setTargetTriple(TargetTripleStr);

    DenseSet<Value *> LiveOut, Globals;
    SmallVector<Value *, 16> LiveIn;

    auto RevTopoChop = getTopoChop(Chop, StartBB, BackEdges);

    auto handlePhis = [&LiveIn, &LiveOut, &Chop, &Globals, &StartBB, &BackEdges,
                       &PDT, &LastBB, &RevTopoChop](PHINode *Phi) -> int32_t {

        // Add uses of Phi before checking if it is LiveIn
        // What happens if this is promoted ot argument?
        for (auto UI = Phi->use_begin(), UE = Phi->use_end(); UI != UE; UI++) {
            if (auto UIns = dyn_cast<Instruction>(UI->getUser())) {
                auto Tgt = UIns->getParent();
                if (!Chop.count(Tgt)) {
                    // errs() << "Adding LO: " << *Ins << "\n";
                    if (!PDT->dominates(LastBB, UIns->getParent())) {
                        // Phi at the begining of path is a live-in,
                        // if it is used outside then don't care as
                        // the argument to the accelerated chop can be
                        // sent to the consumer outside the chop.
                        if (Phi->getParent() != StartBB) {
                            LiveOut.insert(Phi);
                        }
                    }
                } else {
                    // Handle loop-back Phi's
                    auto distance =
                        [&RevTopoChop](SmallVector<BasicBlock *, 16> &SV,
                                       BasicBlock *ToBB) -> uint32_t {
                            uint32_t I = 0;
                            for (auto &BB : RevTopoChop) {
                                if (BB == ToBB)
                                    return I;
                                I++;
                            }
                            assert(false && "Unreachable");
                        };
                    // Find the topo position of Tgt
                    // If it is topologically before the phi def, then it is a
                    // use
                    // across a backedge which we must add as live-out
                    auto PosTgt = distance(RevTopoChop, Tgt);
                    auto PosPhi = distance(RevTopoChop, Phi->getParent());
                    if (PosTgt > PosPhi) {
                        LiveOut.insert(Phi);
                    }
                }
            }
        } // End of handling for LiveOut

        if (Phi->getParent() == StartBB) {
            LiveIn.push_back(Phi);
            return 1;
        }

        uint32_t Num = 0;
        for (uint32_t I = 0; I < Phi->getNumIncomingValues(); I++) {
            auto *Blk = Phi->getIncomingBlock(I);
            auto *Val = Phi->getIncomingValue(I);
            if (Chop.count(Blk)) {
                if (auto *VI = dyn_cast<Instruction>(Val)) {
                    if (Chop.count(VI->getParent()) == 0) {
                        LiveIn.push_back(Val);
                        Num += 1;
                    }
                } else if (auto *AI = dyn_cast<Argument>(Val)) {
                    LiveIn.push_back(AI);
                    Num += 1;
                } else if (auto *GV = dyn_cast<GlobalVariable>(Val)) {
                    errs() << *GV << "\n";
                    Globals.insert(GV);
                }
            }
        }
        return Num;
    };

    // Collect the live-ins and live-outs for the Chop
    uint32_t PhiLiveIn = 0;
    for (auto &BB : Chop) {
        for (auto &I : *BB) {
            if (auto Phi = dyn_cast<PHINode>(&I)) {
                PhiLiveIn += handlePhis(Phi);
                continue;
            }
            liveInHelperStatic(Chop, LiveIn, Globals, &I);
            // Live-Outs
            if (auto Ins = dyn_cast<Instruction>(&I)) {
                for (auto UI = Ins->use_begin(), UE = Ins->use_end(); UI != UE;
                     UI++) {
                    if (auto UIns = dyn_cast<Instruction>(UI->getUser())) {
                        if (!Chop.count(UIns->getParent())) {
                            // errs() << "Adding LO: " << *Ins << "\n";
                            // Need to reason about this check some more.
                            if (!PDT->dominates(LastBB, UIns->getParent()) &&
                                !isa<GetElementPtrInst>(Ins)) {
                                LiveOut.insert(Ins);
                            }
                        }
                    }
                }
            }
        }
    }

    // Add the live-out which is the return value from the
    // chop if it exists since we return void.

    auto *LastT = LastBB->getTerminator();
    if (auto *RT = dyn_cast<ReturnInst>(LastT)) {
        if (auto *Val = RT->getReturnValue()) {
            LiveOut.insert(Val);
        }
    }

    StatsFile << "\"phi_live_in\" : " << PhiLiveIn << ",\n";
    StatsFile << "\"tot_live_in\" : " << LiveIn.size() << ",\n";
    StatsFile << "\"tot_live_out\" : " << LiveOut.size() << ",\n";

    // auto LastBlock = BlockMap[*P.Seq.rbegin()];

    auto VoidTy = Type::getVoidTy(Mod->getContext());

    std::vector<Type *> ParamTy;
    // Add the types of the input values
    // to the function's argument list
    for (auto Val : LiveIn)
        ParamTy.push_back(Val->getType());

    FunctionType *StFuncType = FunctionType::get(VoidTy, ParamTy, false);

    // Create the trace function
    Function *StaticFunc = Function::Create(
        StFuncType, GlobalValue::ExternalLinkage, "__static_func", Mod);

    // Create an external function which is used to
    // model all guard checks.
    auto Int1Ty = IntegerType::getInt1Ty(Mod->getContext());
    FunctionType *GuFuncType = FunctionType::get(VoidTy, Int1Ty, false);

    // Create the guard function
    Function *GuardFunc = Function::Create(
        GuFuncType, GlobalValue::ExternalLinkage, "__guard_func", Mod);

    // // Create the Output Array as a global variable
    uint32_t LiveOutSize = 0;
    const DataLayout *DL = Mod->getDataLayout();
    for_each(LiveOut.begin(), LiveOut.end(),
             [&LiveOutSize, &DL](const Value *Val) {
                 LiveOutSize += DL->getTypeStoreSize(Val->getType());
             });
    ArrayType *CharArrTy =
        ArrayType::get(IntegerType::get(Mod->getContext(), 8), LiveOutSize);
    auto *Initializer = ConstantAggregateZero::get(CharArrTy);
    GlobalVariable *LOA =
        new GlobalVariable(*Mod, CharArrTy, false, GlobalValue::CommonLinkage,
                           Initializer, "__live_outs");

    LOA->setAlignment(8);

    StatsFile << "\"live_out_size\" : " << LiveOutSize << ",\n";

    staticHelper(StaticFunc, GuardFunc, LOA, LiveIn, LiveOut, Globals,
                 RevTopoChop, BackEdges, Mod->getContext(), StatsFile);

    StripDebugInfo(*Mod);

    error_code EC;
    string Name = (P.Id) + string(".static.ll");
    raw_fd_ostream File(Name, EC, sys::fs::OpenFlags::F_RW);
    Mod->print(File, nullptr);
    File.close();

    assert(!verifyModule(*Mod, &errs()) && "Module verification failed!");

    PassManagerBuilder PMB;
    PMB.OptLevel = 3;
    PMB.SLPVectorize = false;
    PMB.BBVectorize = false;
    PassManager PM;
    PMB.populateModulePassManager(PM);
    PM.run(*Mod);

    Name = (P.Id) + string(".static.O3.ll");
    raw_fd_ostream File3(Name, EC, sys::fs::OpenFlags::F_RW);
    Mod->print(File3, nullptr);
    File3.close();

    DEBUG(errs() << "Verifying " << P.Id << "\n");
    // Dumbass verifyModule function returns false if no
    // errors are found. Ref "llvm/IR/Verifier.h":46
    assert(!verifyModule(*Mod, &errs()) && "Module verification failed!");
    delete Mod;
}

void GraphGrok::makeSeqGraph(Function &F) {
    PostDomTree = &getAnalysis<PostDominatorTree>(F);
    AA = &getAnalysis<AliasAnalysis>();

    map<string, BasicBlock *> BlockMap;

    typedef pair<BasicBlock *, BasicBlock *> Key;
    typedef pair<uint64_t, APInt> Val;
    auto PairCmp =
        [](const Key &A, const Key &B) -> bool { 
            return A.first < B.first || 
                (A.first == B.first && A.second < B.second); 
        };
    map<Key, Val, decltype(PairCmp)> FrequentChops(PairCmp);
    map<Key, vector<string>, decltype(PairCmp)> ChopTraceMap(PairCmp);

    for (auto &BB : F)
        BlockMap[BB.getName().str()] = &BB;

    for (auto &P : Sequences) {
        if (GenerateTrace == "unset")
            StatsFile.open(string("stats/") + (P.Id) + string(".og.json"),
                           ios::out);
        else if (GenerateTrace == "dynamic")
            StatsFile.open(string("stats/") + (P.Id) + string(".tr.json"),
                           ios::out);
        else
            StatsFile.open(string("stats/") + (P.Id) + string(".cp.json"),
                           ios::out);

        assert(StatsFile.is_open() && "Could not write stats");
        StatsFile << "{\n";

        StatsFile << "\"function\" : \"" << F.getName().str() << "\",\n";
        StatsFile << "\"path\" : " << P.Id << ",\n";
        StatsFile << "\"freq\" : " << P.Freq << ",\n";
        StatsFile << "\"type\" : " << P.PType << ",\n";
        StatsFile << "\"num_merge_blocks\" : " << P.Seq.size() << ",\n";

        uint64_t TotalBlockSize = 0;
        for (auto BName : P.Seq)
            TotalBlockSize += BlockMap[BName]->size();
        StatsFile << "\"total_ins\" : " << TotalBlockSize << ",\n";
        StatsFile << "\"avg_block_size\" : "
                  << double(TotalBlockSize) / P.Seq.size() << ",\n";

        // Update the chop histogram
        bool NewChop = false;
        if (GenerateTrace == "static") {
            auto *ChopBegin = BlockMap[P.Seq.front()];
            auto *ChopEnd = BlockMap[P.Seq.back()];

            if (FrequentChops.count(make_pair(ChopBegin, ChopEnd)) == 0) {
                FrequentChops.insert(
                    make_pair(make_pair(ChopBegin, ChopEnd),
                              make_pair(0, APInt(256, StringRef("0"), 10))));
                ChopTraceMap.insert(
                    make_pair(make_pair(ChopBegin, ChopEnd), vector<string>()));

                NewChop = true;
            }
            auto K = make_pair(ChopBegin, ChopEnd);
            FrequentChops[K].first += 1;
            FrequentChops[K].second =
                FrequentChops[make_pair(ChopBegin, ChopEnd)].second +
                (P.Freq * TotalBlockSize);
            ChopTraceMap[K].push_back(P.Id);
        }

        // This has to be done on the IR *before*
        // any modifications are done -- like removing
        // Phis (d'oh).
        if (GenerateTrace == "static" && NewChop) {

            StatsFile << "\"chop\" : {\n";
            generateStaticGraphFromPath(P, BlockMap, PostDomTree, StatsFile);

            StatsFile << "},\n\"end\" : \"end\" }";
            StatsFile.close();
            continue;
        }

        if (GenerateTrace == "static") {
            StatsFile.close();
            continue;
        }

        BoostGraph SeqChain(0);
        constructGraph(P, SeqChain, BlockMap);
        labelGraphLevel(SeqChain);
        writeGraph(P, SeqChain, string("orig"));

        // Zero out the level, since it is used for traversal
        // later and the analyses depends on the fact that
        // removed vertices are marked as level zero.
        BGL_FORALL_VERTICES(V, SeqChain, BoostGraph)
        SeqChain[V].Level = 0;

        // Only remove the conversion operations if we are
        // not generating IR will
        // break after removing these operations.
        if (GenerateTrace == "unset") {
            liveInLiveOut(PostDomTree, AA, P, SeqChain, BlockMap, StatsFile);
            removePHI(P, SeqChain, BlockMap, StatsFile);
            removeConvert(P, SeqChain, BlockMap, StatsFile);
            separateBranches(P, SeqChain, BlockMap, StatsFile);
            labelGraphLevel(SeqChain);
            // writeGraph(P, SeqChain, string("label"));

            StatsFile << "\"num_vertices\" : " << num_vertices(SeqChain)
                      << ",\n";
            StatsFile << "\"connected_vertices\" : "
                      << dfsTraverse(SeqChain).size() << ",\n";

            writeGraphClust(SeqChain, P);

            auto Chains = decomposeHelper(SeqChain);

            analyseSpatialSlack(P, SeqChain, BlockMap, StatsFile);
            analyseExecUnit(P, SeqChain, BlockMap, StatsFile);
            analyseTemporalSlack(P, SeqChain, BlockMap, Chains, StatsFile);
            analyseChains(P, SeqChain, BlockMap, Chains, StatsFile);
            analyseGeneral(P, SeqChain, BlockMap, StatsFile);
            writeGraph(P, SeqChain, string("post"));
            StatsFile << "\"end\" : \"end\" }";
        } else if (GenerateTrace == "dynamic") {
            liveInLiveOut(PostDomTree, AA, P, SeqChain, BlockMap, StatsFile);
            removePHI(P, SeqChain, BlockMap, StatsFile);
            // writeGraph(P, SeqChain, string("phi"));
            // Need to label the graph, since we want to
            // iterate by level when generating the trace.
            if (!stripFunctions(SeqChain)) {
                labelGraphLevel(SeqChain);
                // writeGraph(P, SeqChain, string("label"));
                StatsFile << "\"trace\" : {\n";
                generateTraceFromPath(P, SeqChain, BlockMap);
                StatsFile << "},\n\"end\" : \"end\" }";
            } else {
                errs() << "Cannot trace " << P.Id
                       << ", contains indirect call.\n";
            }
        }

        StatsFile.close();
    }

    if (GenerateTrace == "static") {
        // Print frequent chops
        ofstream AllChopStat("stats/chops.json", ios::out);
        assert(AllChopStat.is_open() && "Could not open output for chop stats");
        AllChopStat << "{\n";
        errs() << "Unique Chops: " << FrequentChops.size() << "\n";
        AllChopStat << "\"num_chops\" : " << FrequentChops.size() << ",\n";
        AllChopStat << "\"chops\" : [\n";
        bool First = true;
        for (auto &KV : FrequentChops) {
            errs() << KV.first.first->getName() << "->"
                   << KV.first.second->getName() << " (" << KV.second.first
                   << "," << KV.second.second.toString(10, false) << ")\n";
            if (!First)
                AllChopStat << ",";
            AllChopStat << "{";
            AllChopStat << "\"num_paths\" : " << KV.second.first << ",\n";
            AllChopStat << "\"cum_freq\" : "
                        << KV.second.second.toString(10, false) << ",\n";
            AllChopStat << "\"ids\" : [";
            bool Second = true;
            for (auto S : ChopTraceMap[KV.first]) {
                if (!Second)
                    AllChopStat << ",";
                AllChopStat << "\"" << S << "\"";
                Second = false;
            }
            AllChopStat << "]}";
            First = false;
        }
        AllChopStat << "],";
        AllChopStat << "\"end\" : \"end\"\n}";
        AllChopStat.close();
    }
}

bool GraphGrok::runOnModule(Module &M) {
    for (auto &F : M)
        //if (F.getName() == TargetFunction)
        if (isTargetFunction(F, FunctionList))
            makeSeqGraph(F);

    return false;
}

char GraphGrok::ID = 0;

VertexType getOpType(Instruction &I) {

    if (isa<IntrinsicInst>(&I))
        return VertexType::INTRIN;

    switch (I.getOpcode()) {
    // Terminators
    case Instruction::Ret:
        return VertexType::RET;

    case Instruction::Br: {
        if (dyn_cast<BranchInst>(&I)->isUnconditional())
            return VertexType::UBR;
    }
    case Instruction::Switch:
    case Instruction::IndirectBr:
        return VertexType::CBR;

    case Instruction::Select:
        return VertexType::SELECT;

    // Standard binary operators...
    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::Mul:
    case Instruction::UDiv:
    case Instruction::SDiv:
    case Instruction::URem:
    case Instruction::SRem:
    case Instruction::ICmp:
    case Instruction::FCmp:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    // Logical operators...
    case Instruction::And:
    case Instruction::Or:
    case Instruction::Xor:
        return VertexType::INT;

    case Instruction::FAdd:
    case Instruction::FRem:
    case Instruction::FDiv:
    case Instruction::FMul:
    case Instruction::FSub:
        return VertexType::FP;

    // Memory instructions...
    case Instruction::Alloca:
    case Instruction::Load:
    case Instruction::Store:
    case Instruction::AtomicCmpXchg:
    case Instruction::AtomicRMW:
    case Instruction::Fence:
        return VertexType::MEM;

    case Instruction::GetElementPtr:
        return VertexType::GEP;

    case Instruction::PHI:
        return VertexType::PHI;

    case Instruction::Call:
    case Instruction::Invoke:
        return VertexType::FUNC;

    // Convert instructions...
    case Instruction::Trunc:
    case Instruction::FPTrunc:
    case Instruction::SExt:

    case Instruction::ZExt:
    case Instruction::FPExt:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
    case Instruction::IntToPtr:
    case Instruction::PtrToInt:
    case Instruction::BitCast:
    case Instruction::AddrSpaceCast:
        return VertexType::CONVERT;

    // Other instructions...
    case Instruction::ExtractElement:
    case Instruction::InsertElement:
    case Instruction::ShuffleVector:
        return VertexType::VECTOR;

    case Instruction::ExtractValue:
    case Instruction::InsertValue:
        return VertexType::AGG;

    case Instruction::VAArg:
    case Instruction::LandingPad:
    case Instruction::Resume:
    case Instruction::Unreachable:
    default:
        return VertexType::OTHER;
    }
}
