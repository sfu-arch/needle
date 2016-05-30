#ifndef COMMON_H
#define COMMON_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/InstVisitor.h"

#include <string>

using namespace llvm;
using namespace std;

namespace common {

// Functions
void generateBinary(llvm::Module &m, const std::string &outputFilename,
                    char optLevel, llvm::cl::list<std::string> &libPaths,
                    llvm::cl::list<std::string> &libraries);
void saveModule(llvm::Module &m, llvm::StringRef filename);
void link(const std::string &objectFile, const std::string &outputFile,
          char optLevel, llvm::cl::list<std::string> &libPaths,
          llvm::cl::list<std::string> &libraries);
void compile(llvm::Module &, std::string, char);
llvm::DenseSet<std::pair<const llvm::BasicBlock *, const llvm::BasicBlock *>>
getBackEdges(llvm::BasicBlock *);
llvm::DenseSet<std::pair<const llvm::BasicBlock *, const llvm::BasicBlock *>>
getBackEdges(llvm::Function &);
void optimizeModule(llvm::Module *);
void lowerSwitch(llvm::Module &, llvm::StringRef);
void lowerSwitch(llvm::Function &);
void breakCritEdges(llvm::Module &, llvm::StringRef);
void breakCritEdges(llvm::Function &);
void printCFG(llvm::Function &);
bool checkIntrinsic(llvm::CallSite &);
bool isSelfLoop(const llvm::BasicBlock *);
llvm::SetVector<llvm::Loop *> getLoops(llvm::LoopInfo *);
void writeModule(llvm::Module *, std::string);
void writeFunctionDFG(llvm::Function&); 

}

namespace helpers {

// Classes
class DFGPrinter : public FunctionPass, public InstVisitor<DFGPrinter> {
        friend class InstVisitor<DFGPrinter>;
        void visitFunction(Function& F);
        void visitBasicBlock(BasicBlock& BB);
        void visitInstruction(Instruction& I);
    public:
        static char ID;
        DFGPrinter() : FunctionPass(ID) {}
        bool doInitialization(Module& ) override;
        bool doFinalization(Module& ) override;
        bool runOnFunction(Function& ) override;
        void getAnalysisUsage(AnalysisUsage& AU) const override {
            AU.setPreservesAll();
        }
};
    
}
#endif
