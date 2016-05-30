clang -c -emit-llvm -fno-inline -g -O2 -fno-vectorize -fno-slp-vectorize simple.c
epp -epp-fn=foo simple.bc -o simple
./simple 1 2 3
epp -epp-fn=foo simple.bc -p=path-profile-results.txt

