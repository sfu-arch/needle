#include <cstdint>
#include <cstdio>
#include <fstream>
#include <unordered_map>

#include "epprt.h"
#include <map>

extern "C" {

// This macro allows us to prefix strings so that they are less likely to
// conflict with existing symbol names in the examined programs.
// e.g. EPP(entry) yields PaThPrOfIlInG_entry
#define EPP(X) PaThPrOfIlInG_##X

std::map<__int128, uint64_t> EPP(path);

void EPP(logPath2)(__int128 Val) { EPP(path)[Val] += 1; }

void EPP(save)() {
    FILE *fp = fopen("path-profile-results.txt", "w");
    fprintf(fp, "%lu\n", EPP(path).size());
    for (auto &KV : EPP(path)) {
        uint64_t low = (uint64_t)KV.first;
        uint64_t high = (KV.first >> 64);
        // Print the hex values with a 0x prefix messes up
        // the APInt constructor.
        fprintf(fp, "%016lx%016lx %lu\n", high, low, KV.second);
    }
    fclose(fp);
}

}