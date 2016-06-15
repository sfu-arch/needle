#ifndef ALTCFG_H
#define ALTCFG_H

#include <llvm/IR/BasicBlock.h>
#include <llvm/ADT/MapVector.h>
#include <llvm/ADT/SetVector.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Debug.h>

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

template <typename T>
class altcfg {
    EdgeListTy Edges;
    map<Edge, T> Weights;
    CFGTy CFG;
    FakeTableTy Fakes;
    public:
    bool add(BasicBlock* Src, BasicBlock* Tgt, 
                BasicBlock* Entry = nullptr, BasicBlock* Exit = nullptr);
    T& operator[](Edge);
    EdgeListTy get() const;
    void print(raw_ostream & os = errs()) const;
    void clear() { Edges.clear(),  Weights.clear(), CFG.clear(); }
};


template <typename T>
EdgeListTy 
altcfg<T>::get() const {
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


template <typename T>
bool
altcfg<T>::add(BasicBlock* Src, BasicBlock* Tgt, 
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

    auto& CFG = this->CFG;
    auto insertCFG = [&CFG](BasicBlock* Src, BasicBlock* Tgt) {
        if(CFG.count(Src) == 0) {
            CFG.insert({Src, SuccListTy()});
        }
        CFG[Src].insert(Tgt);
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


template <typename T>
void 
altcfg<T>::print(raw_ostream& os) const {
    os << "Alternate CFG for EPP\n";

    uint64_t Ctr = 0;
    for(auto &E : get()) {
        os << Ctr++ << " " << SRC(E)->getName() 
           << "->" << TGT(E)->getName() << "\n";
    }
}

}
#endif
