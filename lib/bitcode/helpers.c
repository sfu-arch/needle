#include <stdint.h>
#include <stdio.h>

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
                /*printf("%p <- %lu", (void *)addr, val);*/
            }
        }
    }
}

void
__success() {
    printf("success\n"); 
}

void
__fail() {
    printf("fail\n"); 
}

void 
___attribute__ ((noinline)) _InstruMem_load(uint64_t id, char* ptr) {
    asm("");
}

void 
___attribute__ ((noinline)) _InstruMem_store(uint64_t id, char* ptr) {
    asm("");
}
