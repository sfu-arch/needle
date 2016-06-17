#define DEBUG_TYPE "epp_encode"
#include "AltCFG.h"

namespace altepp {

inline void
altcfg::initWt(Edge E, APInt Val = APInt(128, 0, true)) {
    Weights[E] = Val; 
}

static APInt dir(const Edge& E, const Edge& F) {
    if ( SRC(E) == nullptr && TGT(E) == nullptr)
        return APInt(128, 1, true);
    else if (SRC(E) == TGT(F) || TGT(E) == SRC(F))
        return APInt(128, 1, true);
    else
        return APInt(128, -1, true);
}

static void incDFSHelper(APInt Events, BasicBlock *V, Edge E,
                         const EdgeListTy  &ST,
                         const EdgeListTy  &Chords,
                         EdgeWtMapTy &Val,
                         EdgeWtMapTy &Inc) {
    //  for each f belongs to T : f != e and v = tgt( f ) do
    //      DFS( Dir(e, f ) * events + Events( f ) , src( f ) , f )
    //  od
    //  for each f belongs to T : f != e and v = src( f ) do
    //      DFS( Dir(e, f ) * events + Events( f ) , tgt( f ) , f )
    //  od
    //  for each f belongs to E − T : v = src( f ) or v = tgt( f ) do
    //      Increment( f ) : = Increment( f ) + Dir(e, f ) * events
    //  fi

    for (auto &F : ST) {
        if (E != F && V == TGT(F))
            incDFSHelper(dir(E, F) * Events + Val[F], SRC(F), F, ST, Chords,
                         Val, Inc);
    }

    for (auto &F : ST) {
        if (E != F && V == SRC(F))
            incDFSHelper(dir(E, F) * Events + Val[F], TGT(F), F, ST, Chords,
                         Val, Inc);
    }

    for (auto &C : Chords) {
        if (V == SRC(C) || V == TGT(C))
            Inc[C] = Inc[C] + dir(E, C) * Events;
    }
}

void
altcfg::computeIncrement(EdgeWtMapTy& Inc,
        BasicBlock* Entry, BasicBlock* Exit,
        EdgeListTy& Chords, EdgeListTy& ST) {
        
    Chords.push_back({Exit,Entry});
    for (auto &C : Chords)
        Inc.insert({C,APInt(128, 0, true)});

    incDFSHelper(APInt(128, 0, true), Entry, 
            {nullptr, nullptr}, ST, Chords, Weights, Inc);

    initWt({Exit, Entry});

    for (auto &C : Chords) {
        Inc[C] = Inc[C] + Weights[C];
    }
}

EdgeListTy
altcfg::getChords(EdgeListTy& ST) const {
    DenseSet<Edge> SpanningEdges;
    for(auto &E : ST)
        SpanningEdges.insert(E);

    EdgeListTy Chords;
    for(auto &E : get()) {
        if(SpanningEdges.count(E) == 0) {
            Chords.push_back(E);
        }
    }
    return Chords;
}

EdgeWtMapTy
altcfg::getIncrements(BasicBlock* Entry, BasicBlock* Exit) {
    EdgeWtMapTy Inc;
    auto ST = getSpanningTree(Entry);
    auto C = getChords(ST);
    computeIncrement(Inc, Entry, Exit, C, ST);
    return Inc;
}

void 
altcfg::spanningHelper(BasicBlock* ToVisit, 
        EdgeListTy& ST, DenseSet<BasicBlock*>& Seen) {
    Seen.insert(ToVisit);
    for (auto S : succs(ToVisit)) {
        if (!Seen.count(S)) {
            ST.push_back({ToVisit, S});
            spanningHelper(S, ST, Seen);
        }
    }
}

EdgeListTy
altcfg::getSpanningTree(BasicBlock* Entry) {
    EdgeListTy SpanningTree;
    DenseSet<BasicBlock *> Seen;
    spanningHelper(Entry, SpanningTree, Seen);
    assert(SpanningTree.size() == CFG.size() 
            && "SpanningTree missing some nodes!");
    return SpanningTree; 
}

APInt&
altcfg::operator[](const Edge& E ) {
    return Weights[E];
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

    auto insertCFG = [this](BasicBlock* Src, BasicBlock* Tgt) {
        if(CFG.count(Src) == 0) {
            CFG.insert({Src, SuccListTy()});
        }
        CFG[Src].insert(Tgt);
        initWt({Src, Tgt});
    };

    insertCFG(Src, Tgt);
    SuccCache.erase(Src);
    Edges.push_back({Src, Tgt});

    // This is an edge which needs to segmented
    if( Entry && Exit ) {
        Fakes[{Src, Tgt}] = {{Src, Exit}, {Entry, Tgt}};
        insertCFG(Src, Exit);
        insertCFG(Entry, Tgt);
        SuccCache.erase(Entry);
    }
    return true;
}

SmallVector<BasicBlock*, 4>
altcfg::succs(const BasicBlock* B) {  
    assert(CFG.count(B) && "Block does not exist in CFG");
    if(SuccCache.count(B) == 0) {
        SmallVector<BasicBlock*, 4> R;
        for(auto &E : get()) {
            if(SRC(E) == B)
                R.push_back(TGT(E));
        }
        SuccCache.insert({B, R});
        return R;
    } else {
        return SuccCache.lookup(B);
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
    DenseSet<BasicBlock*> Nodes;
    for(auto &E : get()) {
        os << "\tNode" << SRC(E) << " -> Node" << TGT(E) << " [style=solid,"
            << " label=\"" << Weights.find({SRC(E), TGT(E)})->second << "\"];\n";
        Nodes.insert(SRC(E));
        Nodes.insert(TGT(E));
    }
    for(auto &N : Nodes) {
        os << "\tNode" << N << " [shape=record, label=\""
            << N->getName().str() << "\"];\n";
    }
    os << "}\n";
}

}
