#!/bin/bash

# TODO : Check for epp and clang e$xecutable in PATH
EPP=`which epp`
CLANG=`which clang`
DIS=`which llvm-dis`
FN="main"

pushd ../test > /dev/null
for x in `ls *.c`; do
    BCNAME=`echo ${x} | sed 's/\.c/\.bc/'`
    ${CLANG} -c -g -emit-llvm -O2 -o ${BCNAME} ${x} 
    ARGS="${BCNAME} -epp-fn=${FN} -o test"
    ${EPP} ${ARGS} 
done
popd  > /dev/null
