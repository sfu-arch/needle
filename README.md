### Program Analysis for the Synthesis of Hardware Accelerators (PASHA)

### Dependencies 

1. LLVM 3.8.0
2. Boost 1.55  
3. CMake 2.8.8  
4. Doxygen 1.7.6 (optional)

### Build 
0. Download LLVM 3.8.0 source from [here](http://llvm.org/releases/download.html). Also download the Clang source code.   
    a. `$ tar xvf llvm-3.8.0.src.tar.xz`  
    b. `$ tar xvf cfe-3.8.0.src.tar.xz`  
    c. `$ mv cfe-3.8.0.src llvm-3.8.0.src/tools/clang`  
1. Compile LLVM using CMake as described [here](http://llvm.org/docs/CMake.html). CMake needs to be run with additional options.  
    Options  
    a. `LLVM_ENABLE_EH=ON`  
    b. `LLVM_ENABLE_RTTI=ON`  
    Example  
    `$ mkdir llvm-build && cd llvm-build`  
    `$ cmake -DLLVM_ENABLE_EH=ON -DLLVM_ENABLE_RTTI=ON <path/to/llvm-3.6.2.src>`  
    `$ make -j <cpu_count>`  
2. Compile boost with `regex` and `graph` libraries.  
3. Compile _PASHA_ with CMake options  
    a. `BOOST_ROOT`=`<boost/dir>`  
    b. `LLVM_DIR`=`<llvm/dir>`  
    c. `LLVM_ENABLE_EH=ON`  
    d. `LLVM_ENABLE_RTTI=ON`  
    Example  
    `$ mkdir pasha-build && cd pasha-build`  
    `$ cmake -DLLVM_ENABLE_EH=ON -DLLVM_ENABLE_RTTI=ON -DLLVM_DIR=<path/to/llvm-build/share/llvm/cmake> -DBOOST_ROOT=<path/to/boost>`  
    `$ make -j <cpu_count>`  
4. Generate Doxygen (Optional) : If doxygen is detected on the system, `doc` target is available to generate documentation. Run `make doc` to generate documentation.
5. 32-bit runtime (Optional) :
    To work with 32 bit bitcode on a 64 bit system, pass `-DRT32=ON` while running `cmake`. This will make the toolchain produce 32 bit executables. Note number of paths we can support is reduced by a factor of 2, i.e 2^64.


