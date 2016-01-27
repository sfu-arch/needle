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

pushd stage > /dev/null
for x in {1..7}; do
    echo "Running Test ${x}"
    cp ../src/${x}.c .
    BCNAME=${x}.bc
    ${CLANG} -c -g -emit-llvm -O3 -o ${BCNAME} ${x}.c
    ARGS="${BCNAME} -epp-fn=${FN} -o test"
    ${EPP} ${ARGS} 
    IN=`cat ../input/${x}.in`
    ./test ${IN} 2>&1 > /dev/null
    ARGS="${BCNAME} -epp-fn=${FN} -p=path-profile-results.txt -o test"
    ${EPP} ${ARGS} 2> ${x}.out
    DIFF=`diff -q ${x}.out ../gold/${x}.gold > /dev/null`
    if [ "$DIFF" != "" ]; then
        echo "FAIL"
    else
        echo "PASS"
    fi
    rm -f test* *.txt *.bc *.c *.out
done
popd > /dev/null

export PATH=$SAVE_PATH
export LD_LIBRARY_PATH=$SAVE_LD
