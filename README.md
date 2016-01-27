### Program Analysis for the Synthesis of Hardware Accelerators (PASHA)

### Dependencies 

1. LLVM 3.6.2  
2. Boost 1.55  

### Build 

1. Compile LLVM using CMake as described [here](http://llvm.org/docs/CMake.html) with additional options.  
    a. `ENABLE_EH=ON`  
    b. `ENABLE_RTTI=ON`  
2. Compile boost with `regex` and `graph` libraries.  
3. Compile _PASHA_ with CMake options  
    a. `BOOST_ROOT`=`<boost/dir>`  
    b. `LLVM_DIR`=`<llvm/dir>`  
    c. `ENABLE_EH=ON`  
    d. `ENABLE_RTTI=ON`  

### Testing
The folder tests contains a script `run-tests.sh`. Please modify the variables in the script to point to executables in the correct directories. 
