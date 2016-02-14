#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/IR/Verifier.h"

#include <string>
#include <thread>
#include <fstream>

#include "AllInliner.h"
#include "Namer.h"
#include "GraphGrok.h"
#include "MicroWorkloadExtract.h"
#include "AllInliner.h"
#include "Namer.h"

using namespace std;
using namespace llvm;
using namespace llvm::sys;
using namespace grok;
using namespace mwe;

cl::opt<string> InPath(cl::Positional, cl::desc("<Module to analyze>"),
                       cl::value_desc("bitcode filename"), cl::Required);

cl::opt<string> SeqFilePath("seq",
                            cl::desc("File containing basic block sequences"),
                            cl::value_desc("filename"),
                            cl::init("epp-sequences.txt"));

cl::opt<int> NumSeq("num", cl::desc("Number of sequences to analyse"),
                    cl::value_desc("positive integer"), cl::init(3));

//cl::opt<string> TargetFunction("fn", cl::Required,
                               //cl::desc("Target function name"),
                               //cl::value_desc("string"));

//cl::opt<int> MaxNumPaths("max", cl::desc("Maximum number of paths to analyse"),
                         //cl::value_desc("Integer"), cl::init(10));
                         
cl::list<std::string> FunctionList("fn", cl::value_desc("String"),
                                   cl::desc("List of functions to instrument"),
                                   cl::OneOrMore, cl::CommaSeparated);

bool isTargetFunction(const Function &f,
                      const cl::list<std::string> &FunctionList) {
    if (f.isDeclaration())
        return false;
    for (auto &fname : FunctionList)
        if (fname == f.getName())
            return true;
    return false;
}


int main(int argc, char **argv, const char **env) {
    // This boilerplate provides convenient stack traces and clean LLVM exit
    // handling. It also initializes the built in support for convenient
    // command line option handling.

    sys::PrintStackTraceOnErrorSignal();
    llvm::PrettyStackTraceProgram X(argc, argv);
    LLVMContext &context = getGlobalContext();
    llvm_shutdown_obj shutdown;

    // InitializeAllTargets();
    // InitializeAllTargetMCs();
    // InitializeAllAsmPrinters();
    // InitializeAllAsmParsers();
    // cl::AddExtraVersionPrinter(TargetRegistry::printRegisteredTargetsForVersion);
    cl::ParseCommandLineOptions(argc, argv);

    // Construct an IR file from the filename passed on the command line.
    SMDiagnostic err;
    unique_ptr<Module> module(parseIRFile(InPath.getValue(), err, context));

    if (!module.get()) {
        errs() << "Error reading bitcode file.\n";
        err.print(argv[0], errs());
        return -1;
    }

    PassManager pm;
    pm.add(new DataLayoutPass());
    pm.add(new llvm::AssumptionCacheTracker());
    pm.add(createLowerSwitchPass());
    pm.add(createLoopSimplifyPass());
    pm.add(createBasicAliasAnalysisPass());
    pm.add(createTypeBasedAliasAnalysisPass());
    pm.add(new LoopInfo());
    pm.add(new llvm::CallGraphWrapperPass());
    pm.add(new epp::PeruseInliner());
    pm.add(new epp::Namer());
    pm.add(llvm::createPostDomTree());
    pm.add(new DominatorTreeWrapperPass());
    //pm.add(new grok::GraphGrok(SeqFilePath, NumSeq));
    pm.add(new mwe::MicroWorkloadExtract(SeqFilePath, NumSeq));
    pm.add(createVerifierPass());
    pm.run(*module);

    return 0;
}
