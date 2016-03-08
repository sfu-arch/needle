#include <stdint.h>
#include <stdio.h>

char undo_log[96];

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


int main() {
    uint64_t* ptr = (uint64_t*)undo_log;
    ptr[0] = 0x667;
    ptr[4] = 0x666;
    uint32_t val = __prev_store_exists((char *)&ptr[0], (char *)&ptr[4]);
    printf("%d", val);
    return 0;
}
