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

Needle analyses frequently executed sequences of basic blocks (paths) to reason about which to outline. There are python scripts which evaluate the epp-sequence.txt file to filter out the path or braid blocks which can be outlined. The scripts are present in `examples/scripts/*.py`.


### Outlining

### Synthesis

