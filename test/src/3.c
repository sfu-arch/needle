
#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char **argv) {
  for (int i = 0, e = atoi(argv[1]); i < e; ++i) {
    if ((i + argc) % 3) {
      printf("Truey\n");
    } else {
      printf("Falsey\n");
    }
  }
  return 0;
}


