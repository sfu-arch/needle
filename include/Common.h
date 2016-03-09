#ifndef COMMON_H 
#define COMMON_H 

#include "llvm/IR/Module.h"
#include "llvm/ADT/StringRef.h"
#include <string>

namespace common {

void generateBinary(llvm::Module &m, const std::string &outputFilename);
void saveModule(llvm::Module &m, llvm::StringRef filename); 
void link(const std::string &objectFile, const std::string &outputFile);
void compile(llvm::Module &m, std::string outputPath);

}

#endif
