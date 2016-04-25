#ifndef COMMON_H 
#define COMMON_H 

#include "llvm/IR/Module.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/CallSite.h"

#include <string>

namespace common {
    void generateBinary(llvm::Module &m, const std::string &outputFilename,
            char optLevel, llvm::cl::list<std::string>& libPaths, 
            llvm::cl::list<std::string>& libraries);

    void saveModule(llvm::Module &m, llvm::StringRef filename); 

    void link(const std::string &objectFile, const std::string &outputFile,
            char optLevel, llvm::cl::list<std::string>& libPaths, 
            llvm::cl::list<std::string>& libraries);

    void compile(llvm::Module&, std::string, char);

    llvm::DenseSet<std::pair<const llvm::BasicBlock *, const llvm::BasicBlock *>>
            getBackEdges(llvm::BasicBlock *);

    llvm::DenseSet<std::pair<const llvm::BasicBlock *, const llvm::BasicBlock *>>
            getBackEdges(llvm::Function&);

    void optimizeModule(llvm::Module*);

    void lowerSwitch(llvm::Module&, llvm::StringRef);

    bool checkIntrinsic(llvm::CallSite&);
}

#endif
