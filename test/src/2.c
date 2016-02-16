#include <stdio.h>

int foo(int argc) {
    return argc * 4;
}

int main(int argc, char* argv[]) {
    printf("%d\n", foo(argc));
    return 0;
}
