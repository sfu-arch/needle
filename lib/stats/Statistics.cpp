#define DEBUG_TYPE "pasha_statistics"

#include "Statistics.h"
#include <cassert>
#include <string>
#include "json11.hpp"

using namespace llvm;
using namespace std;
using namespace pasha;

bool Statistics::doInitialization(Module &M) {
    return false;
}

static void
criticalPathLength(Function &F) {
    
}

static void 
conditionOpStats(Function &F) {

}

static void 
generalStats(Function &F) {

}

// Statistics :
// 1. Branch conditions based on memomry access
// 2. Ops only used for evaluation of conditions
// 3. Critical path length in ops

bool Statistics::runOnFunction(Function &F) {
    criticalPathLength(F);
    conditionOpStats(F);
    generalStats(F);
    return false;
}

bool Statistics::doFinalization(Module& M) {
    return false;
}

char Statistics::ID = 0;
static RegisterPass<Statistics> X("", "PASHA - Statistics");
