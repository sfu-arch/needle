
#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char **argv) {
  for (int i = 0, e = atoi(argv[1]); i < e; ++i) {
    if (!((i + argc) % 3)) {
      printf("True\n");
      goto extra;
    } else {
      printf("False\n");
    }

    if (i + argc % 2) {
      printf("Bacon\n");
    } else {
      printf("Pancakes\n");
    }

    if ((i + argc) % 5) {
      printf("Finn\n");
    } else {
      printf("Jake\n");
extra:
      printf("Lady Rainicorn\n");
    }
  }

  return 0;
}


