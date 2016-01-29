#include <cstdint>
#include <cstdio>
#include <fstream>
#include <unordered_map>
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "epprt.h"

extern "C" {

// This macro allows us to prefix strings so that they are less likely to
// conflict with existing symbol names in the examined programs.
// e.g. EPP(entry) yields PaThPrOfIlInG_entry
#define EPP(X) PaThPrOfIlInG_##X

// std::unordered_map<uint64_t, uint64_t> EPP(pathMap);
// std::unordered_map<uint64_t, uint64_t> EPP(selfLoopMap);

// void
// EPP(selfLoop)(uint64_t loopID) {
//   EPP(selfLoopMap)[loopID] += 1;
// }
//
// void
// EPP(logPath)(uint64_t loopID, uint64_t pathID) {
//   auto ID = loopID + pathID;
//   EPP(pathMap)[ID] += 1;
// }
//
// void
// EPP(save)() {
//   std::ofstream txtfile("path-profile-results.txt", std::ios::out);
//   txtfile << EPP(pathMap).size() << "\n";
//   for(auto &KV : EPP(pathMap))
//       txtfile << KV.first << " " << KV.second << "\n";
//   txtfile.close();
//
//   std::ofstream txtfile2("self-loop.txt", std::ios::out);
//   txtfile2 << EPP(selfLoopMap).size() << "\n";
//   for(auto &KV : EPP(selfLoopMap))
//       txtfile2 << KV.first << " " << KV.second << "\n";
//   txtfile2.close();
// }

llvm::DenseMap<llvm::APInt, uint64_t, llvm::DenseMapAPIntKeyInfo> EPP(pathMap);
std::unordered_map<uint64_t, uint64_t> EPP(selfLoopMap);
llvm::APInt EPP(Counter)(256, llvm::StringRef("0"), 10);

// Maintaining the counter in the runtime depends on 2 things:
// 1. Only one function is being instrumented
// 2. The function being instrumented is not called recursively
// 3. Mutual recursion WILL break this.

void EPP(incCount)(uint64_t qw0, uint64_t qw1, uint64_t qw2, uint64_t qw3) {
    uint64_t QW[] = {qw0, qw1, qw2, qw3};
    llvm::APInt Inc(256, QW);
    EPP(Counter) += Inc;
}

void EPP(selfLoop)(uint64_t loopID) { EPP(selfLoopMap)[loopID] += 1; }

void EPP(logPath)() {
    //std::ofstream txtfile("path-log.txt", std::ios::app);
    //txtfile << EPP(Counter).toString(10, false) << "\n" ;
    //txtfile.close();
    EPP(pathMap)[EPP(Counter)] += 1;
    EPP(Counter).clearAllBits();
}

void EPP(save)() {
    std::ofstream txtfile("path-profile-results.txt", std::ios::out);
    txtfile << EPP(pathMap).size() << "\n";
    for (auto &KV : EPP(pathMap))
        txtfile << KV.first.toString(10, false) << " " << KV.second << "\n";
    txtfile.close();

    std::ofstream txtfile2("self-loop.txt", std::ios::out);
    txtfile2 << EPP(selfLoopMap).size() << "\n";
    for (auto &KV : EPP(selfLoopMap))
        txtfile2 << KV.first << " " << KV.second << "\n";
    txtfile2.close();
}
}
