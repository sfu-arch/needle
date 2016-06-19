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
typedef MapVector<const BasicBlock *, SuccListTy> CFGTy;
typedef MapVector<Edge, pair<Edge, Edge>> FakeTableTy;
typedef DenseMap<const BasicBlock*, SmallVector<BasicBlock*, 4>> SuccCacheTy;
typedef MapVector<Edge, APInt> EdgeWtMapTy;

class altcfg {
    EdgeListTy Edges;
    EdgeWtMapTy Weights;
    CFGTy CFG;
    FakeTableTy Fakes;
    SuccCacheTy SuccCache;
    EdgeListTy get() const;
    EdgeListTy getSpanningTree(BasicBlock *);
    void spanningHelper(BasicBlock*, EdgeListTy&, 
            DenseSet<BasicBlock*>&);
    EdgeListTy getChords(EdgeListTy&) const;
    void computeIncrement(EdgeWtMapTy&, BasicBlock*, BasicBlock*, 
            EdgeListTy&, EdgeListTy&);
  public:
    EdgeWtMapTy getIncrements(BasicBlock*, BasicBlock*);
    bool add(BasicBlock* Src, BasicBlock* Tgt, 
                BasicBlock* Entry = nullptr, BasicBlock* Exit = nullptr);
    APInt& operator[](const Edge&);
    void initWt(const Edge, const APInt);
    void print(raw_ostream & os = errs()) const;
    void dot(raw_ostream& os = errs()) const;
    SmallVector<BasicBlock*, 4> succs(const BasicBlock*);
    void clear() { Edges.clear(),  Weights.clear(), 
        CFG.clear(); SuccCache.clear(); }
};

}
#endif
