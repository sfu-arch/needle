## Needle Toolchain Overview

The Needle toolchain comprises of four phases  

0. Preprocess
1. Profiling
2. Analysis
3. Outlining (Extraction)
4. Backend/Synthesis

### Preprocess

Needle performs preprocessing of the LLVM whole program bitcode to make it easier to profile and outline the program regions. Preprocessing is only performed on the function of interest (specified on the command line). The preprocessing stages are listed here:  

1. Inlining - Needle inlines all functions into their callsite for a given parent function. This is done by overriding the getInlineCost function for all callsite within the parent function. The code is present in `lib/Inliner/Inliner.cpp`.     

2. Switch-Case constructs are converted to if statements - This is required by phase 3 (outlining) which assumes that each basic block may have at most two successors.  The actual transformation is performed by the lower-switch pass present in LLVM. This pass is mostly robust but it does break on certain codes in 403.gcc. This is invoked by the Simplify pass in `lib/simplify/Simplify.cpp`.    

3. [Critical Edges](https://en.wikipedia.org/wiki/Control_flow_graph#Special_edges) are split using the LLVM break critical edges pass. This is invoked by the Simplify pass in `lib/simplify/Simplify.cpp`.    

4. Basic Block naming pass: All basic blocks are assigned a name "__unk__" (unkown). Since all transformations are deterministic, running these four stages always produces bitcode with the same basic block names.   

At this point the bitcode is ready to be consumed by the rest of the pipeline. All four preprocessing steps are run before any profiling or transformation can be applied to the bitcode.

### Profiling

Needle implements efficient path profiling. The driver code is present in tool/epp/main.cpp. The profiling phase contains three stages. 

1. Instrumentation - The control flow graph of the function is analysed to enumerate the path ids and insert instrumentation along certain edges. The number of statically enumerated paths is worst case bounded exponentially to the number of branches. If the number of unique paths exceeds 2^128 (2^64 on 32 bit systems), the epp tool will crash. The passes that perform the encoding and instrumentation are `lib/epp/EPPEncoding.cpp` and `lib/epp/EPPProfile.cpp`.      

2. Profiling - The instrumented binary will be executed with a runtime which collects the path profile data. There are two shared libraries provided which offer two different modes of data collection. The first is an aggregate mode, where the aggregate execution count of each path is dumped at the end of the profiling run. The second is a Run Length Encoded mode which dumps out a trace of paths being executed in run length encoding. This stage produces a path-profile-results.txt file which contains the profiled data. The code for the runtime is present in `lib/epp/Runtime*.cpp`.     

3. Decoding - With the profiled data and the original bitcode (after preprocessing). The decoding phase generates epp-sequences.txt with each path decoded into their basic block sequences.    

Paths which have `unacceleratable` features are not output to `epp-sequences.txt`. The check is implemented in `lib/epp/EPPDecode.cpp:46` in function `pathCheck`.

### Analysis

Needle analyses frequently executed sequences of basic blocks (paths) to reason about which to outline. There are python scripts which evaluate the epp-sequence.txt file to filter out the path or braid blocks which can be outlined. The scripts are present in `examples/scripts/*.py`. The scripts produce `path-seq-N.txt` or `braid-seq-N.txt`. The format of these files are the same as the `epp-sequence.txt` files. However, they only contain the basic blocks for a single path or blocks for multiple paths which belong to the same braid. Remember, a braid contains paths which start and end with the same basic block pair. 

### Outlining

The Needle toolchain outlines basic blocks specified in the previous stage. Outlining is the process of copying the selected basic blocks into a function of their own. It can be the thought of as the inverse of inlining. However, outlining multiple basic blocks implies predefined choices being made for conditional branches for some basic blocks. Thus the outlining process involves instrumentation of side effect causing instructions so that program state can be restored if the predetermined branch direction is incorrect. When outlining blocks, the set of blocks are specified in topological order and it is guaranteed that all blocks are reachable from the first (topologically ordered) block. Speculation support (restoring program state) is implemented using a undo log mechanism (see `Undo Log Design`). All writes from the outlined blocks are instrumented to capture the existing value in memory. If a side exit (speculation failure) is taken, the original values are written back into memory. The structure of the code is described below:

1. A new empty module is created. The `NeedleOutliner::extract` function creates a new `LLVM::Function` which includes the outlined basic blocks. For each conditional branch for which a successor basic block is not included in the outlined function, a dummy guard function is added which anchors the check in place. See `Guard Lowering`.    

2. All values which are def'ed outside the region and used within the region are called `Live In` values. Values which are def'ed within the region and used outside the region are called `Live Out` values.     

3. The outlined function (offload function) is created. The structure is described in `Outline Function Characteristics. In general, live in's are passed as parameters and live out's are returned as a struct. The struct is allocated before the outlined function invocation and a pointer to the struct is passed into the outlined function.    

4. A new function pass manager is instantiated to run subsequent processing passes on the new outlined function. This is required as alias analysis needs to be run on the outlined function to implement undo logging. The two passes run during this phase implement a) lowering of guards to control flow for generic processors b) adding support for the undo log. Note that if hardware supports speculative execution (store buffer / checkpointing) these passes are not required. These passes transform the bitcode so that the workload can be executed correctly on general purpose processors. See `Undo Log Design` and `Guard Lowering`.    

5. A function call is inserted in the first basic block of the outlined region. This invokes the outlined function. The returned value (boolean) is checked to determine if a) *true* - the outlined function succeeded, get the live out values from the struct passed by reference b) *false* - the outlined function failed, program state needs to be restored and the undo function is called and then execution resumes from the original basic block.    

#### Outline Function Characteristics

1. Naming: The outlined function is called `__offload_func_XXX` where `XXX` is the path id.
2. Parameters: Live In values, pointers to global variables (text and data segment), pointer to undo log buffer, pointer to Live Out structure type. 
3. Return (boolean): True if outlined function completed successfully, False if a side exit was taken.

#### Undo Log Design 

In order to restore program state after speculation failure three pieces of information need to saved for every write from the outlined region. They are   

1. The address of the write   
2. The value at the address prior to the first write in program order   
3. The width of the value being written   

These are maintained in two arrays 

1. Undo log buffer - This contains the address and value for each escaping write. The address and values widths are worst case sized to 8 bytes each.  
2. Undo width buffer - This contains the width of each write, in the same order as the undo log buffer.  

The information is saved in multiple locations due to the way they are used. The Undo Log Buffer needs to be updated at runtime by instrumentation in the outlined function. Whereas, the undo width buffer can be statically populated at compile time. To allow for multiple outlined regions in the same program, a single worst case sized undo log buffer is used. At each invocation of the outlined function, a `memset(0)` is performed for the width of the undo log buffer. The size (number of writes that need to be restored) per outlined function is also saved separately as a global constant. 

Rollback of program state is performed in the following manner:

1. The entry block for that region (a block which dominates all the other blocks) is split at the earliest possible point.
2. The offload function call is inserted. A test checks the return from the offload function. A true condition leads to a new basic block which reads the values from the live out struct and updates the necessary program state. A false condition invokes the `__undo_mem` function and then resumes execution of the original control flow graph. 
3. The `__undo_mem_` function defined in `bitcode/helpers.c`. It takes 3 arguments, where the first argument is always the same pointer to the global undo buffer. The second argument is the number of stores to rollback and the third argument is the width of each store saved in a separate buffer.    
4. The undo log buffer is dynamically allocated to be as large as the 2 times the number of maximum number of stores across all offload functions. The amount of memory reset (using `memset`) to zero prior to using the same undo log buffer is equal to the number of stores in the current offload function.  

#### Guard Lowering

Control flow assertions in Needle are modeled by removing the LLVM IR conditional branch instruction and replacing it with a dummy function call to `__guard_func`. The guard function takes two arguments. The first argument is the value produced by the comparison i.e the check and the second argument is the value it should produce. Thus the second paramenter encodes when the true or false condition which checks whether the assertion passes or not. The lowering for general purpose processors is to return false as soon as possible when the check fails. The insertion of guard intrinsics happens in `lib/needle/NeedleOutliner.cpp:465` and the lowering in `lib/needle/NeedleHelper:189`.

### Backend -- Synthesis and Simulation

While Needle produces executables which can run on general purpose processors, the goal is to be able to use the offload function as input to a backend simulation or synthesis toolchain. To this end we have used Needle as part of the [Chainsaw - Micro'16](https://github.com/sfu-arch/chainsaw) project. In this project we generated a dataflow graph of operations. Chainsaw only targets paths and model control flow assertions as part of the simulation.   

We have also integrated Needle with LegUp, a high level synthesis tool from Univesity of Toronto which uses LLVM to compile C to Verilog. The toolchain to target Verilog is available online as [Bhima](https://github.com/sfu-arch/bhima).  
