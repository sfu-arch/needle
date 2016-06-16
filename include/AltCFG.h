#ifndef ALTCFG_H
#define ALTCFG_H

#include <llvm/IR/BasicBlock.h>
#include <llvm/ADT/MapVector.h>
#include <llvm/ADT/SetVector.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Debug.h>
#include <llvm/ADT/APInt.h>

#include <memory>
#include <map>
#include <set>
#include <cassert>

using namespace llvm;
using namespace std;


namespace altepp {

#define SRC(E) \
    (E.first)

#define TGT(E) \
    (E.second)

typedef pair<BasicBlock*, BasicBlock*> Edge;
typedef SmallVector<Edge, 32> EdgeListTy; 
typedef SetVector<BasicBlock*, vector<BasicBlock*>,
                  DenseSet<BasicBlock*>> SuccListTy;
typedef MapVector<BasicBlock *, SuccListTy> CFGTy;
typedef MapVector<Edge, pair<Edge, Edge>> FakeTableTy;

class altcfg {
    EdgeListTy Edges;
    map<Edge, APInt> Weights;
    CFGTy CFG;
    FakeTableTy Fakes;
    public:
    bool add(BasicBlock* Src, BasicBlock* Tgt, 
                BasicBlock* Entry = nullptr, BasicBlock* Exit = nullptr);
    uint64_t& operator[](Edge);
    EdgeListTy get() const;
    void setWt(Edge, APInt);
    void print(raw_ostream & os = errs()) const;
    void dot(raw_ostream& os = errs()) const;
    void clear() { Edges.clear(),  Weights.clear(), CFG.clear(); }
};


void
altcfg::setWt(Edge E, APInt Val = APInt(128, 0, true)) {
    Weights[E] = Val; 
}


EdgeListTy 
altcfg::get() const {
    EdgeListTy Ret;
    for(auto &E : Edges) {
        if(Fakes.count(E)) {
            Ret.push_back(Fakes.lookup(E).first);
            Ret.push_back(Fakes.lookup(E).second);
        } else {
            Ret.push_back(E);
        }
    } 
    return Ret;
}

bool
altcfg::add(BasicBlock* Src, BasicBlock* Tgt, 
        BasicBlock* Entry , BasicBlock* Exit ) {

    assert(!((uint64_t)Entry ^ (uint64_t)Exit) && 
            "Both Entry and Exit must be defined or neither");

    // Enforces the constraint that only one edge can exist between 
    // two basic blocks. 
    if(CFG.count(Src) &&
            CFG[Src].count(Tgt)) {
        DEBUG(errs() << "Edge " << Src->getName() << "->" << 
                Tgt->getName() << " already exists in CFG\n");
        return false; 
    }

    // This is required as in C++11 lambda does not capture this
    // pointer. 
    auto insertCFG = [this](BasicBlock* Src, BasicBlock* Tgt) {
        if(CFG.count(Src) == 0) {
            CFG.insert({Src, SuccListTy()});
        }
        CFG[Src].insert(Tgt);
        setWt({Src, Tgt});
    };

    insertCFG(Src, Tgt);
    Edges.push_back({Src, Tgt});

    // This is an edge which needs to segmented
    if( Entry && Exit ) {
        Fakes[{Src, Tgt}] = {{Src, Exit}, {Entry, Tgt}};
        insertCFG(Src, Exit);
        insertCFG(Entry, Tgt);
    }
}

void 
altcfg::print(raw_ostream& os) const {
    os << "Alternate CFG for EPP\n";
    uint64_t Ctr = 0;
    for(auto &E : get()) {
        os << Ctr++ << " " << SRC(E)->getName() 
           << "->" << TGT(E)->getName() << " " 
           << Weights.find(E)->second << "\n";
    }
}

void
altcfg::dot(raw_ostream& os) const {
     os << "digraph \"AltCFG\" {\n label=\"AltCFG\";\n";
    for(auto &KV : CFG) {
        os << "\tNode" << KV.first << " [shape=record, label=\""
            << KV.first->getName().str() << "\"];\n";
        for(auto &S : KV.second) {
            os << "\tNode" << KV.first << " -> Node" << S << "[style=solid,"
                << " label=\"" << Weights.find({KV.first, S})->second << "\"];\n";
        }
    }
    os << "}\n";
}

}
#endif
