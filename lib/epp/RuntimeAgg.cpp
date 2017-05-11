#include <cstdint>
#include <cstdio>
#include <map>

extern "C" {

// This macro allows us to prefix strings so that they are less likely to
// conflict with existing symbol names in the examined programs.
// e.g. EPP(entry) yields PaThPrOfIlInG_entry
#define EPP(X) PaThPrOfIlInG_##X

#ifdef __LP64__

std::map<__int128, uint64_t> EPP(path64);

void EPP(init64)() {}

void EPP(logPath64)(__int128 Val) { EPP(path64)[Val] += 1; }

void EPP(save64)() {
    FILE *fp = fopen("path-profile-results.txt", "w");
    fprintf(fp, "%lu\n", EPP(path64).size());
    for (auto &KV : EPP(path64)) {
        uint64_t low  = (uint64_t)KV.first;
        uint64_t high = (KV.first >> 64);
        // Print the hex values with a 0x prefix messes up
        // the APInt constructor.
        fprintf(fp, "%016lx%016lx %lu\n", high, low, KV.second);
    }
    fclose(fp);
}

#endif

std::map<uint64_t, uint64_t> EPP(path32);

void EPP(init32)() {}

void EPP(logPath32)(uint64_t Val) { EPP(path32)[Val] += 1; }

void EPP(save32)() {
    FILE *fp = fopen("path-profile-results.txt", "w");
    fprintf(fp, "%lu\n", EPP(path32).size());
    for (auto &KV : EPP(path32)) {
        // Print the hex values with a 0x prefix messes up
        // the APInt constructor.
        fprintf(fp, "%016lx %lu\n", KV.first, KV.second);
    }
    fclose(fp);
}
}
