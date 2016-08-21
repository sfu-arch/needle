#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>

/** ANSI Terminal Colors **/
#define RED "\x1b[31m"
#define GREEN "\x1b[32m"
#define YELLOW "\x1b[33m"
#define BLUE "\x1b[34m"
#define MAGENTA "\x1b[35m"
#define CYAN "\x1b[36m"
#define RESET "\x1b[0m"

/** Compile : $ gcc -Wno-format -I<path/to/LogTypes.def> log_reader.c -o reader
 ** Usage : $ ./reader livein.dump.bin liveout.dump.bin succ.dump.bin | less -r  
 **/

/**
 ** LogTypes.def is generated by mwe tool when run with the -log
 ** command line option. The file is unique to each path as it
 ** defines the elements present as struct members for the livein
 ** and liveout binary dump formats.
 ** Add -Wno-format to suppress warnings
 ** Do NOT use clang to compile this as it seems to discard packed
 ** attribute for struct.
 **/

#ifdef __clang__
#error "clang doesn't packed attributes for structs, use gcc"
#endif

#include "LogTypes.def"

struct LIVEIN {
#define X(type, name, format) type name;
    X_FIELDS_LIVEIN
#undef X
} __attribute__((packed));

struct LIVEOUT {
#define X(type, name, format) type name;
    X_FIELDS_LIVEOUT
#undef X
} __attribute__((packed));

off_t fsize(const char *filename) {
    struct stat st;
    if (stat(filename, &st) == 0)
        return st.st_size;
    return -1;
}

int main(int argc, char *argv[]) {
    FILE *infp = fopen(argv[1], "rb");
    FILE *outfp = fopen(argv[2], "rb");
    FILE *succfp = fopen(argv[3], "rb");
    assert(infp && outfp && succfp && "Could not open files");

    off_t fsz = fsize(argv[1]);
    uint64_t iters = fsz / sizeof(struct LIVEIN);

    printf("Num Iters: %lu\n", iters);

    struct LIVEIN *inptr = (struct LIVEIN *)malloc(sizeof(struct LIVEIN));
    struct LIVEOUT *outptr = (struct LIVEOUT *)malloc(sizeof(struct LIVEOUT));
    bool *succptr = (bool *)malloc(sizeof(bool));

    printf("------\n");
    for (uint64_t c = 0; iters > 0; c++, iters--) {
        fread((void *)inptr, sizeof(struct LIVEIN), 1, infp);
        fread((void *)outptr, sizeof(struct LIVEOUT), 1, outfp);
        fread((void *)succptr, sizeof(bool), 1, succfp);
        if(*succptr) {
            printf(GREEN "SUCCESS " CYAN "#%lu\n" RESET, c);
        } else {
            printf(RED "FAIL " CYAN "#%lu\n" RESET, c);
        }
        #define X(type, name, format) \
            printf(YELLOW "> " RESET #name " " format "\n", inptr->name);
                X_FIELDS_LIVEIN
        #undef X
        #define X(type, name, format) \
            printf(BLUE "< " RESET #name " " format "\n", outptr->name);
                X_FIELDS_LIVEOUT
        #undef X
        printf("------\n");
    }

    free(inptr);
    free(outptr);
    free(succptr);
    return 0;
}
