#include <cstdint>
#include <cstdio>
#include <map>

extern "C" {

// This macro allows us to prefix strings so that they are less likely to
// conflict with existing symbol names in the examined programs.
// e.g. EPP(entry) yields PaThPrOfIlInG_entry
#define EPP(X) PaThPrOfIlInG_##X

#ifdef __LP64__

__int128 EPP(PathId64);
uint64_t EPP(Counter64);
FILE *fp64 = nullptr;

void EPP(init64)() {
    EPP(PathId64) = -1;
    fp64          = fopen("path-profile-trace.txt", "w");
}

void EPP(logPath64)(__int128 Val) {
    if (EPP(PathId64) == -1) {
        EPP(PathId64)  = Val;
        EPP(Counter64) = 1;
    } else if (EPP(PathId64) == Val) {
        EPP(Counter64) += 1;
    } else {
        uint64_t low  = (uint64_t)EPP(PathId64);
        uint64_t high = (EPP(PathId64) >> 64);
        fprintf(fp64, "%016lx%016lx %lu\n", high, low, EPP(Counter64));
        EPP(PathId64)  = Val;
        EPP(Counter64) = 1;
    }
}

void EPP(save)() { fclose(fp64); }

#endif

int64_t EPP(PathId32);
uint64_t EPP(Counter32);
FILE *fp32 = nullptr;

void EPP(init32)() {
    EPP(PathId32) = -1;
    fp32          = fopen("path-profile-trace.txt", "w");
}

void EPP(logPath32)(uint64_t Val) {
    if (EPP(PathId32) == -1) {
        EPP(PathId32)  = Val;
        EPP(Counter32) = 1;
    } else if (EPP(PathId32) == Val) {
        EPP(Counter32) += 1;
    } else {
        fprintf(fp32, "%016lx %lu\n", EPP(PathId32), EPP(Counter32));
        EPP(PathId32)  = Val;
        EPP(Counter32) = 1;
    }
}

void EPP(save32)() { fclose(fp32); }
}
