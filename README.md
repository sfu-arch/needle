## NEEDLE: Leveraging Program Analysis to extract Accelerators from Whole Programs
[![Build Status](https://travis-ci.org/sfu-arch/needle.svg?branch=master)](https://travis-ci.org/sfu-arch/needle)


### Dependencies 

1. LLVM 3.8
2. CMake 2.8.8  
3. Doxygen 1.7.6 (optional)
4. c++14 compatible compiler, gcc-5 or greater should suffice

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

* [doc](./doc) -- Documentation for logging infrastructure and overlapping paths
* [cmake](./cmake) -- Additional CMake modules
* [examples](./examples) -- Sample workloads drawn from SPEC, PARSEC and PERFECT
* [include](./include) -- Headers for the project
* [support](./support) -- Tool to read dumped binary logs
* [tools](./tools) -- EPP and NEEDLE tools
* [lib](./lib) 
  * [epp](./lib/epp) -- Module passes for efficient path profiling
  * [inliner](./lib/inliner) -- Module passes for aggressive inlining 
  * [namer](./lib/namer) -- Module pass for naming all LLVM Values
  * [common](./lib/common) -- Common shared routines across libraries 
  * [bitcode](./lib/bitcode) -- Runtime which implements software speculation support and logging
  * [needle](./lib/needle) -- Needle framework libraries

The NEEDLE repository is organized as an LLVM project. It generates two binaries as tools. They are `epp` and `needle`. The `epp` tool takes whole program bitcode as input and produces an instrumented binary. This binary is then executed to collect an aggregate path profile. The path profile is decoded to enumerate constituent basic blocks. The paths enumerated can now be analysed. Selected paths are specified by enumerating basic block sequences to the `needle` tool. The `needle` tool creates a new function containing only the specified blocks. Branches where only a single successor is included are converted to early exits from the function. The function can then be used as a template for different backends.

### Running the Examples

The following stages (Makefile targets) are available for each workload  

1. setup - Preprocess bitcode from the repository
2. epp-inst - Instrument the bitcode for Path Profiling
3. epp-run - Run the instrumented bitcode to collect the aggregate path execution frequencies
4. epp-decode - Decode each path to it's constituent basic block sequences
5. needle-path - Outline the path sequence into a new offload function, adding rollback code where required
6. needle-braid - Outline multiple path sequences into a new braid offload function, adding rollback code where required
7. needle-run-path - Execute the software binary where a selected path has been outlined
8. needle-run-braid - Execute the software binary where a braid has been outlined

The top level Makefile inside `examples/workloads` provides all these targets. They toolchain flow is in the order in which the targets are listed. All stages up to epp-decode need only be run once for a given input. Workload specific options are specified in the Makefiles present in each workload directory. The structure of each workload is described below.

### Workload Structure

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

You can use NEEDLE to collect the aggregate path profile of your workload (`epp-inst` -> `epp-run` -> `epp-decode`). Then analyse the path profile to select the basic blocks from one or more paths to target as the offload function. This serves as input to the next stage which creates the offload function (`needle-path` or `needle-braid`).

#### How do I compile my workload into bitcode?

Any C/C++ workload can be converted into a single monolithic bitcode file for analysis. Researchers have even compiled the FreeBSD kernel into bitcode. Please take a look at the Whole Program LLVM project.  

https://github.com/travitch/whole-program-llvm

#### What is an offload function?

An offload function is a created automatically by the `needle` tool by outlining selected basic blocks. For basic block where some successors are not outlined an early exit is generated. Software speculation support is added to ensure that program state is restored if an early return from the function occurs.

#### How does NEEDLE implement software speculation?

NEEDLE implements software speculation by maintaining an `undo log`. An `undo log` entry is created for every write to memory from the outlined region. NEEDLE instruments every write with a read to capture the original value present in the memory location. If speculation along the path or braid fails, the values recorded in the `undo log` are written back to memory. 

#### What is a backend? Why do I need it?

A backend is responsible for generating code for the accelerator being targeted. In our HPCA paper we use a FPGA backend which translates bitcode to Verilog -- [LegUp](http://legup.eecg.utoronto.ca/). We are currently working out licensing issues so that we can release a modififed version of LegUp which supports NEEDLE. 

A simulation oriented backend was used in Chainsaw (Sharifian et al. MICRO'16), the dataflow graph is extracted from the NEEDLE constructed offload function and then used in the Chainsaw toolchain. The backend is available [online](https://github.com/sfu-arch/chainsaw).

### License 

Dual licensed for industry and academia.   
Industrial users can use this software under the MIT License.
Academic users can use this software under the [CRAPL License](http://matt.might.net/articles/crapl/).
License text for both are provided in the repository.

### Authors

Snehasish Kumar -- <ska124@sfu.ca>  

### Lexicon

- llvm: Low Level Virtual Machine, a compiler framework written in C++
- bitcode: Intermediate Representation file format used by llvm 
- path: Sequence of basic blocks, see epp
- epp: Efficient Path Profiling, Ball and Larus '96
- braid: Paths merged to form a larger abstraction
- offload: A function generated by NEEDLE which contains a path or braid with software speculation support


