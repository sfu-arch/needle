
## NEEDLE: Leveraging Program Analysis to extract Accelerators from Whole Programs
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

### What's in the NEEDLE repository?


### Running the Examples

The following stages are available for each workload  

1. setup - Preprocess bitcode from the repository
2. epp-inst - Instrument the bitcode for Path Profiling
3. epp-run - Run the instrumented bitcode to collect the aggregate path execution frequencies
4. epp-decode - Decode each path to it's constituent basic block sequences
5. needle-path - Outline the path sequence into a new offload function, adding rollback code where required
6. needle-braid - Outline multiple path sequences into a new braid offload function, adding rollback code where required
7. needle-run-path - Execute the software binary where a selected path has been outlined
8. needle-run-braid - Execute the software binary where a braid has been outlined

The top level Makefile inside `examples` provides all these targets. They toolchain flow is in the order in which the targets are listed. All stages up to epp-decode need only be run once for a given input. Workload specific options are specified in the Makefiles present in each workload directory. The structure of each workload is described below.

### Example Workload Structure

Each workload consists of a folder in the examples directory. This contains a Makefile with the following variables

- SUITE: Workload suite it is derived from
- NAME: Workload name
- BC: Path to bitcode file
- CFLAGS: Additional CFLAGS required during compilation
- FUNCTION: Function to profile/target
- LDFLAGS: Additional LDFLAGS required during compilation
- RUNCMD: Command used to execute the workload binary
- PRERUN: Command(s) to execute prior to running the workload 


### How can I use NEEDLE?

#### How do I compile my workload into bitcode?

Any C/C++ workload can be converted into a single monolithic bitcode file for analysis. Researchers have even compiled the FreeBSD kernel into bitcode. Please take a look at the Whole Program LLVM project.  

https://github.com/travitch/whole-program-llvm

### License 

Dual licensed for industry and academia.   
Industrial users can use this software under the MIT License.
Academic users can use this software under the CRAPL License.
License text for both are provided in the repository.

### Lexicon

- llvm: Low Level Virtual Machine, a compiler framework written in C++
- bitcode: Intermediate Representation file format used by llvm 
- path: Sequence of basic blocks, see epp
- epp: Efficient Path Profiling, Ball and Larus '96
- braid: Paths merged to form a larger abstraction


### FAQ
