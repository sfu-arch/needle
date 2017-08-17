#!/bin/bash

if [ ! -d "llvm-4.0" ]; then
    echo "Downloading LLVM (145M)"
    wget http://releases.llvm.org/4.0.0/clang+llvm-4.0.0-x86_64-linux-gnu-ubuntu-14.04.tar.xz
    echo -n "Unpacking LLVM, this can take some time ... "
    tar xf clang+llvm-4.0.0-x86_64-linux-gnu-ubuntu-14.04.tar.xz
    echo "Done"
    mv clang+llvm-4.0.0-x86_64-linux-gnu-ubuntu-14.04 llvm-4.0
    mv clang+llvm-4.0.0-x86_64-linux-gnu-ubuntu-14.04.tar.xz llvm-4.0
else
    echo "llvm-4.0 directory already exists, please remove to redownload LLVM"
fi
