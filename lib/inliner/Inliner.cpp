#include "AllInliner.h"

#define DEBUG_TYPE "inline"

using namespace llvm;
using namespace epp;

extern cl::list<std::string> FunctionList;
extern bool isTargetFunction(const Function &, const cl::list<std::string> &);

InlineCost PeruseInliner::getInlineCost(CallSite CS) {
    Function *Callee = CS.getCalledFunction();

    if (Callee && !Callee->isDeclaration() &&
        isTargetFunction(*(CS.getInstruction()->getParent()->getParent()),
                         FunctionList) &&
        ICA->isInlineViable(*Callee)) {
        InlineStats
            << "Parent: "
            << CS.getInstruction()->getParent()->getParent()->getName().str()
            << "\n";
        InlineStats << "This: " << CS.getCalledFunction()->getName().str()
                    << "\n";
        return InlineCost::getAlways();
    }

    return InlineCost::getNever();
}

bool PeruseInliner::runOnSCC(CallGraphSCC &SCC) {
    bool Changed = false;
    while ((Changed = Inliner::runOnSCC(SCC)))
        ;
    return Changed;
}

void PeruseInliner::getAnalysisUsage(AnalysisUsage &AU) const {
    Inliner::getAnalysisUsage(AU);
}

char PeruseInliner::ID = 0;
static RegisterPass<PeruseInliner> X("",
                                     "Efficient Path Profiling -- AllInliner");
