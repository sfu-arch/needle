#!/bin/bash

if [ ! -d "llvm-3.8" ]; then
    echo "Downloading LLVM (145M)"
    wget http://releases.llvm.org/3.8.1/clang+llvm-3.8.1-x86_64-linux-gnu-ubuntu-14.04.tar.xz
    echo -n "Unpacking LLVM, this can take some time ... "
    tar xf clang+llvm-3.8.1-x86_64-linux-gnu-ubuntu-14.04.tar.xz
    echo "Done"
    mv clang+llvm-3.8.1-x86_64-linux-gnu-ubuntu-14.04 llvm-3.8
    mv clang+llvm-3.8.1-x86_64-linux-gnu-ubuntu-14.04.tar.xz llvm-3.8
else
    echo "llvm-3.8 directory already exists, please remove to redownload LLVM"
fi
