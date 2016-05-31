clang -c -emit-llvm -fno-inline -g -O2 -fno-vectorize -fno-slp-vectorize simple.c
epp -epp-fn=foo simple.bc -o simple
./simple 1 2 3
epp -epp-fn=foo simple.bc -p=path-profile-results.txt
mwe -fn=foo -seq=epp-sequences.txt -trace -u=/home/ska124/working/pasha/lib/bitcode/undo.bc simple.bc -o simple-mwe 
dot -Tpdf dfg.__offload_func_0.dot -o dfg.__offload_func_0.pdf
xdg-open dfg.__offload_func_0.pdf
