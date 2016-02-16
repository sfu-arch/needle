
#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char **argv) {
  for (int j = 0, e = 100; j != e; ++j) {

    for (int i = 0, e = atoi(argv[1]); i < e; ++i) {
      if ((i + argc) % 3) {
        if ((i + argc) % 6) {
          printf("True\n");
        } else {
          printf("Truish\n");
        }
      } else {
        printf("False\n");
      }
      if ((i + argc) % 5) {
        printf("Finn\n");
      } else {
        printf("Jake\n");
      }
    }

  }
  return 0;
}


