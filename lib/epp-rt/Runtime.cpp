#include <cstdint>
#include <cstdio>
#include <fstream>
#include <unordered_map>
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "epprt.h"

#include <map>

extern "C" {

// This macro allows us to prefix strings so that they are less likely to
// conflict with existing symbol names in the examined programs.
// e.g. EPP(entry) yields PaThPrOfIlInG_entry
#define EPP(X) PaThPrOfIlInG_##X

llvm::DenseMap<llvm::APInt, uint64_t, llvm::DenseMapAPIntKeyInfo> EPP(pathMap);
llvm::APInt EPP(Counter)(128, 0, true);


std::map<__int128, uint64_t > EPP(path);

// Maintaining the counter in the runtime depends on 3 things:
// 1. Only one function is being instrumented
// 2. The function being instrumented is not called recursively
// 3. Mutual recursion WILL break this.

void EPP(incCount)(uint64_t qw0, uint64_t qw1) {
    // The constructor for APInt which initializes from an array only 
    // constructs unsigned APInts. Work around this by creating a signed
    // APInt first and then modifying the internal storage.
    llvm::APInt Inc(128, 0, true);
    auto *data = const_cast<uint64_t*>(Inc.getRawData());
    data[0] = qw0; data[1] = qw1;
    EPP(Counter) += Inc;
}

void EPP(logPath2)(__int128 Val) {
    EPP(path)[Val] += 1; 
}

void EPP(logPath)() {
    //std::ofstream txtfile("path-log.txt", std::ios::app);
    //txtfile << EPP(Counter).toString(10, true) << "\n" ;
    //txtfile.close();
    EPP(pathMap)[EPP(Counter)] += 1;
    EPP(Counter).clearAllBits();
}

void EPP(save)() {
    std::ofstream txtfile("path-profile-results.txt", std::ios::out);
    txtfile << EPP(pathMap).size() << "\n";
    for (auto &KV : EPP(pathMap))
        txtfile << KV.first.toString(10, true) << " " << KV.second << "\n";
    txtfile.close();
    
    FILE *fp = fopen("path-profile-results2.txt", "w");
    fprintf(fp, "%lu\n", EPP(path).size());
    for (auto &KV : EPP(path)) {
        uint64_t low = (uint64_t) KV.first;
        uint64_t high = (KV.first >> 64);
        fprintf(fp, "0x%lx%lx %lu\n", high, low, KV.second);
    }
    fclose(fp);
 
}
}
