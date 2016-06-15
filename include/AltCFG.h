#ifndef ALTCFG_H
#define ALTCFG_H

#include <llvm/IR/BasicBlock.h>
#include <llvm/ADT/MapVector.h>
#include <llvm/Support/raw_ostream.h>

#include <memory>
#include <map>
#include <set>

using namespace llvm;
using namespace std;

namespace altepp {

struct Edge; 
typedef shared_ptr<Edge> EdgePtr;
typedef SmallVector<EdgePtr, 32> EdgeListTy; 
typedef MapVector<BasicBlock *, set<EdgePtr>> SuccListTy;

struct Edge {
    BasicBlock *Src, *Tgt;
    pair<EdgePtr, EdgePtr> Fakes; 
    Edge(BasicBlock *S, BasicBlock *T)
        : Src(S), Tgt(T) { Fakes = {nullptr, nullptr}; }
    static EdgePtr make(BasicBlock *S, BasicBlock *T) {
        return make_shared<Edge>(S, T);
    }
};

template <typename T>
class altcfg {
    EdgeListTy Real;
    EdgeListTy Fake;
    map<EdgePtr, T> Weights;
    SuccListTy SuccList;
    EdgePtr add(BasicBlock*, BasicBlock*, EdgeListTy&);
    public:
    EdgePtr add(BasicBlock* Src, BasicBlock* Tgt, 
                BasicBlock* Entry = nullptr, BasicBlock* Exit = nullptr);
    T& operator[](EdgePtr);
    EdgeListTy get() const;
    void print(raw_ostream & os = errs()) const;
    void clear();
};

template <typename T>
inline void
altcfg<T>::clear() {
    Real.clear(), Fake.clear(), Weights.clear(), SuccList.clear();
}

template <typename T>
EdgePtr
altcfg<T>::add(BasicBlock* Src, BasicBlock* Tgt, EdgeListTy& EL) {
    EdgePtr EP = Edge::make(Src, Tgt);
    if(SuccList.count(Src) == 0) {
        SuccList.insert({Src, set<EdgePtr>()});
    }
    SuccList[Src].insert(EP);
    EL.push_back(EP);
    return EP;
}

template <typename T>
EdgePtr
altcfg<T>::add(BasicBlock* Src, BasicBlock* Tgt, 
        BasicBlock* Entry , BasicBlock* Exit ) {
    EdgePtr EP  = add(Src, Tgt, Real);
    if( Entry && Exit ) {
        EdgePtr EP1 = add(Tgt, Exit, Fake);
        EdgePtr EP2 = add(Entry, Src, Fake);
        EP->Fakes = {EP1, EP2};
    }
    return EP;
}


template <typename T>
void 
altcfg<T>::print(raw_ostream& os) const {
    os << "Alternate CFG for EPP\n"
       << "Real Edges\n";

    uint64_t Ctr = 0;
    for(auto &E : Real) {
        os << Ctr++ << " " << E->Src->getName() 
           << "->" << E->Tgt->getName() << "\n";
    }

    os << "Fake Edges\n";
    for(auto &E : Fake) {
        os << Ctr++ << " " << E->Src->getName() 
           << "->" << E->Tgt->getName() << "\n";
    }
}

}
#endif
