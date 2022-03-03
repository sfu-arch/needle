#include <stdio.h>
#include <math.h>
#include <stdlib.h>
int a[10] = {1,-2,3,4,-5,6,7,8,9,10};
int example() {
    int s = 0;
    #pragma unroll 10
    for (int i = 0; i < 10; i++)
    {
        if (a[i]>0) {
            s = s+(2<<a[i]); 
        } else {
          s = s+abs(a[i]);  
        }
    }
    return s;
}

int main(int argc, char** argv){
    printf("%d",example());
}

