#include <stdio.h>

int arr[100] = {0};

void __attribute__ ((noinline)) foo(int *arr, int size) {
    int i = 0;
    for(; i < size; i++) {
        arr[i] = 3 * i;
    }
}

int main(int argc, char* argv[]) {
    foo(arr, 100);
    printf("%d", arr[10*argc]);
    return 0;
}
