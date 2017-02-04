[![Build Status](https://travis-ci.org/sfu-arch/needle.svg?branch=master)](https://travis-ci.org/sfu-arch/needle)

### Dependencies 

1. LLVM 3.8
3. CMake 2.8.8  
4. Doxygen 1.7.6 (optional)

### Build 
1. Clone this repository (or download)    
    `$ git clone git@github.com:sfu-arch/needle.git`
2. Download LLVM    
    `$ cd needle && ./get_llvm.sh && cd ..`
3. Run cmake and make in a separate build folder    
    `$ mkdir needle-build && cd needle-build && cmake ../needle -DLLVM_DIR=../needle/llvm-3.8/share/llvm/cmake && make -j 4`
4. Run an example    
    `$ cd examples/workloads/164.gzip && make needle-run-path`


