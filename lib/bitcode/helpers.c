#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

uint64_t __mwe_success_count = 0;
uint64_t __mwe_fail_count = 0;

FILE * fp_in = 0;
FILE * fp_out = 0;

uint32_t
__prev_store_exists(char* begin, char* loc) {
    char *curr = loc - 16;
    while(begin <= curr) {
        if(*((uint64_t*)curr) == *((uint64_t*)loc)) {
            return 1;
        }
        curr -= 16;
    }
    return 0;
}

void
__undo_mem(char* buffer, uint32_t num_locs) {
    uint32_t buf_size = num_locs * 2 * 8;
    for(uint32_t i = 0; i < buf_size; i+=16){
        uint64_t addr = *((uint64_t*)&buffer[i]);
        uint64_t val = *((uint64_t*)&buffer[i+8]);
        if(addr == 0) {
            continue;
        } else {
            if(!__prev_store_exists(buffer, &buffer[i])) {
                *((uint64_t *)addr) = val;
                /*printf("%p <- %lu\n", (void *)addr, val);*/
            }
        }
    }
}

void
__mwe_dtor() {
   printf("mwe-num-success %lu\n", __mwe_success_count);
   printf("mwe-num-fail %lu\n", __mwe_fail_count);
   fclose(fp_in);
   fclose(fp_out);
}


void
__mwe_ctor() {
    fp_in = fopen("livein.dump.bin", "wb");
    fp_out = fopen("liveout.dump.bin", "wb");
    if(!(fp_in && fp_out)) {
        printf("MWE Ctor : Could not open file\n");
        abort();
    }
    __mwe_success_count = 0;
    __mwe_fail_count = 0;
}

void
__log_in(char* ptr, size_t sz) {
    fwrite(ptr, sizeof(char), sz, fp_in);
}

void
__log_out(char* ptr, size_t sz) {
    fwrite(ptr, sizeof(char), sz, fp_out);
}

void
__success() {
    /*printf("mwe-success\n");*/
    __mwe_success_count++;
}

void
__fail() {
    /*printf("mwe-fail\n");*/
    __mwe_fail_count++;
}

void 
__attribute__ ((__noinline__)) 
    __InstruMem_load(uint64_t id, char* ptr) {
    asm("");
}

void 
__attribute__ ((__noinline__)) 
    __InstruMem_store(uint64_t id, char* ptr) {
    asm("");
}
