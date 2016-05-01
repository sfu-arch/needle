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
#include "llvm/Linker/Linker.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetLibraryInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Scalar.h"

#include <string>
#include <thread>
#include <fstream>

#include "AllInliner.h"
#include "Namer.h"
#include "MicroWorkloadExtract.h"
#include "AllInliner.h"
#include "Namer.h"
#include "Common.h"
#include "LoopConverter.h"

using namespace std;
using namespace llvm;
using namespace llvm::sys;
using namespace mwe;

enum ExtractType { trace, chop };

// MWE-only options

cl::opt<std::string>
    UndoLib("u", cl::desc("Path to the undo library bitcode module"),
            cl::Required);

cl::opt<ExtractType> ExtractAs(cl::desc("Choose extract type, trace / chop"),
                               cl::values(clEnumVal(trace, "Extract as trace"),
                                          clEnumVal(chop, "Extract as chop"),
                                          clEnumValEnd),
                               cl::Required);

cl::opt<string> InPath(cl::Positional, cl::desc("<Module to analyze>"),
                       cl::value_desc("bitcode filename"), cl::Required);

cl::opt<string> SeqFilePath("seq",
                            cl::desc("File containing basic block sequences"),
                            cl::value_desc("filename"),
                            cl::init("epp-sequences.txt"));

cl::opt<int> NumSeq("num", cl::desc("Number of sequences to analyse"),
                    cl::value_desc("positive integer"), cl::init(1));

cl::list<std::string> FunctionList("fn", cl::value_desc("String"),
                                   cl::desc("List of functions to instrument"),
                                   cl::OneOrMore, cl::CommaSeparated);

cl::opt<char> optLevel("O",
                       cl::desc("Optimization level. [-O0, -O1, -O2, or -O3] "
                                "(default = '-O2')"),
                       cl::Prefix, cl::ZeroOrMore, cl::init('2'));

cl::list<string> libPaths("L", cl::Prefix,
                          cl::desc("Specify a library search path"),
                          cl::value_desc("directory"));

cl::list<string> libraries("l", cl::Prefix,
                           cl::desc("Specify libraries to link to"),
                           cl::value_desc("library prefix"));

cl::opt<string> outFile("o", cl::desc("Filename of the instrumented program"),
                        cl::value_desc("filename"), cl::Required);

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

    InitializeAllTargets();
    InitializeAllTargetMCs();
    InitializeAllAsmPrinters();
    InitializeAllAsmParsers();
    cl::AddExtraVersionPrinter(
        TargetRegistry::printRegisteredTargetsForVersion);
    cl::ParseCommandLineOptions(argc, argv);

    // Construct an IR file from the filename passed on the command line.
    SMDiagnostic err;
    unique_ptr<Module> module(parseIRFile(InPath.getValue(), err, context));
    if (!module.get()) {
        errs() << "Error reading bitcode file.\n";
        err.print(argv[0], errs());
        return -1;
    }

    // Load the undo library and link it
    unique_ptr<Module> UndoMod(parseIRFile(UndoLib, err, context));
    if (!UndoMod.get()) {
        errs() << "Error reading undo lib bitcode.\n";
        err.print(argv[0], errs());
        return -1;
    }

    common::optimizeModule(module.get());
    common::lowerSwitch(*module, FunctionList[0]);

    PassManager pm;
    pm.add(new DataLayoutPass());
    pm.add(new llvm::AssumptionCacheTracker());
    pm.add(createLoopSimplifyPass());
    pm.add(createBasicAliasAnalysisPass());
    pm.add(createTypeBasedAliasAnalysisPass());
    pm.add(new llvm::CallGraphWrapperPass());
    pm.add(new epp::PeruseInliner());
    pm.add(new epp::Namer());
    pm.add(new pasha::LoopConverter());
    pm.add(new LoopInfo());
    pm.add(llvm::createPostDomTree());
    pm.add(new DominatorTreeWrapperPass());
    pm.add(new mwe::MicroWorkloadExtract(SeqFilePath, 
                NumSeq, ExtractAs, UndoMod.get()));
    // The verifier pass does not work for some apps (gcc, h264)
    // after linking the original module with the generated one
    // and the undo module. Instead we write out the generated
    // module from the pass itself and discard the original.
    // pm.add(createVerifierPass());
    pm.run(*module);

    return 0;
}
