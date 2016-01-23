#!/bin/bash

SAVE_PATH=$PATH
SAVE_LD=$LD_LIBRARY_PATH

ADD_PATH="/bin:/usr/bin:/home/ska124/Working/llvm-3.6-cmake/bin:/home/ska124/Working/pasha-debug/Debug/bin"
ADD_LD="/home/ska124/Working/pasha-debug/Debug/lib:/usr/local/lib64"

export PATH=$ADD_PATH
export LD_LIBRARY_PATH=$ADD_LD

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
    ./test
    ARGS="${BCNAME} -epp-fn=${FN} -p=path-profile-results.txt -o test"
    ${EPP} ${ARGS} 
done
popd  > /dev/null

export PATH=$SAVE_PATH
export LD_LIBRARY_PATH=$SAVE_LD
