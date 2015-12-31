#ifndef DILWORTH_DECOMPOSE_H
#define DILWORTH_DECOMPOSE_H

#include <vector>
#include <set>
#include <cstdint>
#include <iostream>
#include <boost/graph/transitive_closure.hpp>
#include <boost/graph/max_cardinality_matching.hpp>
#include <boost/property_map/property_map.hpp>

namespace dilworth {
template <typename TGraph> class DilworthDecompose {
  public:
    DilworthDecompose() {}
    std::vector<std::vector<std::uint64_t>> getMinimalChains(const TGraph &G);
};

template <typename TGraph>
std::vector<std::vector<std::uint64_t>>
DilworthDecompose<TGraph>::getMinimalChains(const TGraph &G) {
    typedef typename TGraph::vertex_descriptor Vertex;

    typedef boost::adjacency_list<boost::listS, boost::vecS, boost::undirectedS>
        UndirectedGraph;

    auto num_vertices = boost::num_vertices(G);
    UndirectedGraph UG(2 * num_vertices);

    typename boost::graph_traits<TGraph>::edge_iterator ei, ei_end;
    boost::tie(ei, ei_end) = boost::edges(G);
    for (; ei != ei_end; ++ei) {
        boost::add_edge(boost::source(*ei, G),
                        num_vertices + boost::target(*ei, G), UG);
    }

    std::vector<std::vector<std::uint64_t>> MinimalChains;

    std::vector<Vertex> MateMap(boost::num_vertices(UG));
    bool Match =
        boost::checked_edmonds_maximum_cardinality_matching(UG, &MateMap[0]);

    assert(Match && "Could not find maximal matching");

    auto NullVertex = boost::graph_traits<UndirectedGraph>::null_vertex();

    // std::cout << "Matching: \n";
    // boost::graph_traits<UndirectedGraph>::vertex_iterator vi, vi_end;
    //   for(boost::tie(vi,vi_end) = boost::vertices(UG); vi != vi_end; ++vi)
    //     if (MateMap[*vi] != NullVertex && *vi < MateMap[*vi])
    //       std::cout << "{" << *vi << ", " << MateMap[*vi] - num_vertices <<
    //       "}" << std::endl;

    std::set<Vertex> InChains;
    bool Flag = false;
    do {
        // Find the first non-null and follow chain
        Flag = false;
        for (std::uint64_t I = 0; I < num_vertices; I++) {
            if (MateMap[I] != NullVertex) {
                // Begin a chain
                std::vector<std::uint64_t> Chain;
                auto Next = I;
                do {
                    if (Next >= num_vertices)
                        Next -= num_vertices;
                    if (InChains.count(Next) == 0) {
                        Chain.push_back(Next);
                        InChains.insert(Next);
                    }
                    auto Prev = Next;
                    Next = MateMap[Next];
                    MateMap[Prev] = NullVertex;
                } while (Next != NullVertex);
                MinimalChains.push_back(Chain);
                Flag = true;
            }
        }
    } while (Flag);

    // std::set<Vertex> InChains;
    // for(auto &C : MinimalChains) {
    //     for(auto V : C) {
    //         InChains.insert(V);
    //     }
    // }

    BGL_FORALL_VERTICES_T(V, G, TGraph) {
        if (InChains.count(V) == 0) {
            std::vector<Vertex> C;
            C.push_back(V);
            MinimalChains.push_back(C);
        }
    }

    uint32_t Sum = 0;
    for (auto &M : MinimalChains) {
        Sum += M.size();
    }

    assert(Sum == boost::num_vertices(G) && "All vertices not accounted for");

    return MinimalChains;
}
}

#endif
