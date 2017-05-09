#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>

uint64_t __mwe_success_count  = 0;
uint64_t __mwe_fail_count     = 0;
uint64_t __mwe_addr_log_count = 0;

FILE *fp_in   = 0;
FILE *fp_out  = 0;
FILE *fp_succ = 0;
FILE *fp_mlog = 0;

uint32_t __prev_store_exists(char *begin, char *loc) {
    char *curr = loc - 16;
    while (begin <= curr) {
        if (*((uint64_t *)curr) == *((uint64_t *)loc)) {
            return 1;
        }
        curr -= 16;
    }
    return 0;
}

#define LIST                                                                   \
    X(8)                                                                       \
    X(16)                                                                      \
    X(32)                                                                      \
    X(64)

void __undo_mem(char *buffer, uint32_t num_locs, uint32_t *sizes) {
    uint32_t buf_size = num_locs * 2 * 8;
    for (uint32_t i = 0; i < buf_size; i += 16) {
        uint64_t addr = *((uint64_t *)&buffer[i]);

#define X(sz) uint##sz##_t val##sz = 0;
        LIST
#undef X
            uint32_t sz = sizes[i / 16];
        switch (sz) {
#define X(sz)                                                                  \
    case (sz):                                                                 \
        val##sz = *((uint##sz##_t *)&buffer[i + 8]);                           \
        break;
            LIST
#undef X
                default : abort();
        }

        if (addr == 0) {
            continue;
        } else {
            if (!__prev_store_exists(buffer, &buffer[i])) {
                switch (sz) {
#define X(sz)                                                                  \
    case (sz):                                                                 \
        *((uint##sz##_t *)addr) = val##sz;                                     \
        break;
                    LIST
#undef X
                        default : abort();
                }
                /*printf("%p <- %lu\n", (void *)addr, val);*/
            }
        }
    }
}

#undef LIST

void __mwe_dtor(char **ulog) {

    free(*ulog);

    if(fp_in) {
        printf("mwe-num-success %" PRIu64 "\n", __mwe_success_count);
        printf("mwe-num-fail %" PRIu64 "\n", __mwe_fail_count);
        fclose(fp_in);
        fclose(fp_out);
        fclose(fp_succ);
        fclose(fp_mlog);
    }

}

void __mwe_ctor(char **ulog, size_t ulog_size, int log_enable) {

    *ulog = (char *)malloc(ulog_size);

    if(log_enable) {
        fp_in   = fopen("livein.dump.bin", "wb");
        fp_out  = fopen("liveout.dump.bin", "wb");
        fp_succ = fopen("succ.dump.bin", "wb");
        fp_mlog = fopen("mlog.dump.txt", "w");
        if (!(fp_in && fp_out && fp_succ && fp_mlog)) {
            printf("MWE Ctor : Could not open file\n");
            abort();
        }
        __mwe_success_count = 0;
        __mwe_fail_count    = 0;
    }
}

void __log_in(char *ptr, size_t sz) { fwrite(ptr, sizeof(char), sz, fp_in); }

void __log_out(char *ptr, size_t sz) { fwrite(ptr, sizeof(char), sz, fp_out); }

void __log_succ(bool succ) {
    bool val = succ;
    fwrite(&val, sizeof(bool), 1, fp_succ);
}

void __success() {
    /*printf("mwe-success\n");*/
    __mwe_success_count++;
}

void __mlog(uint64_t addr, uint64_t val, uint64_t sz) {
    fprintf(fp_mlog, "0x%016" PRIx64 " %" PRIu64 " %" PRIu64 "\n", addr, val, sz);
}

void __fail() {
    /*printf("mwe-fail\n");*/
    __mwe_fail_count++;
}
