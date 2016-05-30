#include <stdio.h>

#define SIZE 100

char arr[SIZE] = {1};

int foo(int argc) {
    int sum = 0;
    for(int i = 0; i < SIZE; i++) {
        if( i % argc ) {
            sum += arr[i];
        } else {
            sum -= arr[i];
        }
    }
    return sum;
}

int main(int argc, char *argv[]) {
    printf("%d\n",foo(argc));
    return 0;
}
