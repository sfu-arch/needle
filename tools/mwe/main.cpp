#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TypeBasedAliasAnalysis.h"
#include "llvm/Analysis/ScopedNoAliasAA.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/CFLAliasAnalysis.h"
#include "llvm/Analysis/ScalarEvolutionAliasAnalysis.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Scalar.h"

#include <fstream>
#include <string>
#include <thread>

#include "AllInliner.h"
#include "AllInliner.h"
#include "Common.h"
#include "MicroWorkloadExtract.h"
#include "AliasEdgeWriter.h"
#include "Namer.h"
#include "Simplify.h"

using namespace std;
using namespace llvm;
using namespace llvm::sys;
using namespace mwe;


// MWE-only options

cl::opt<std::string>
    HelperLib("u", cl::desc("Path to the undo library bitcode module"),
              cl::Required);

cl::opt<ExtractType> ExtractAs(cl::desc("Choose extract type, trace / chop"),
                               cl::values(clEnumVal(ExtractType::trace, "Extract as trace"),
                                          clEnumVal(ExtractType::slice, "Extract as slice (chop)"),
                                          clEnumVal(ExtractType::merge, "Extract as merge (chop)"),
                                          clEnumValEnd),
                               cl::Required);

cl::opt<string> InPath(cl::Positional, cl::desc("<Module to analyze>"),
                       cl::value_desc("bitcode filename"), cl::Required);

cl::opt<string> SeqFilePath("seq",
                            cl::desc("File containing basic block sequences"),
                            cl::value_desc("filename"),
                            cl::init("epp-sequences.txt"));

// This option is not used anymore
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

cl::opt<bool> SimulateDFG("simdfg",
                          cl::desc("Generate dfg.*.dot for simulation, add "
                                   "instrumentation to binary for Pintool"),
                          cl::value_desc("boolean"), cl::init(false));

cl::opt<bool> ConvertGlobalsToPointers("global-to-pointer",
                cl::desc("Convert globals in the extracted function to live in pointers"), cl::value_desc("boolean"), cl::init(false));

cl::opt<bool> DumpStats("dump-stats", cl::desc("Pasha stats"),
        cl::value_desc("boolean"), cl::init(false));

bool isTargetFunction(const Function &f,
                      const cl::list<std::string> &FunctionList) {
    if (f.isDeclaration())
        return false;
    for (auto &fname : FunctionList)
        if (fname == f.getName())
            return true;
    return false;
}

void 
runAliasEdgeWriter(Module *M) {
    legacy::PassManager PM;
    PM.add(createBasicAAWrapperPass());
    PM.add(llvm::createTypeBasedAAWrapperPass());
    PM.add(createGlobalsAAWrapperPass());
    PM.add(createSCEVAAWrapperPass());
    PM.add(createScopedNoAliasAAWrapperPass());
    PM.add(createCFLAAWrapperPass());
    PM.add(new aew::AliasEdgeWriter());
    PM.run(*M);
    
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

    vector<unique_ptr<Module>> ExtractedModules;

    common::optimizeModule(module.get());

    legacy::PassManager pm;
    pm.add(new llvm::AssumptionCacheTracker());
    pm.add(createLoopSimplifyPass());
    pm.add(createBasicAAWrapperPass());
    pm.add(createTypeBasedAAWrapperPass());
    pm.add(createGlobalsAAWrapperPass());
    pm.add(createSCEVAAWrapperPass());
    pm.add(createScopedNoAliasAAWrapperPass());
    pm.add(createCFLAAWrapperPass());
    pm.add(new llvm::CallGraphWrapperPass());
    pm.add(new epp::PeruseInliner());
    pm.add(new pasha::Simplify(FunctionList[0]));
    pm.add(new epp::Namer());
    pm.add(new LoopInfoWrapperPass());
    pm.add(llvm::createPostDomTree());
    pm.add(new DominatorTreeWrapperPass());
    pm.add(new mwe::MicroWorkloadExtract(SeqFilePath, NumSeq, 
                                         ExtractedModules));
    pm.add(createVerifierPass());
    pm.run(*module);

    // Use a Composite module instead of linkning into the original
    // as it doesn't work -- no idea why.
    auto Composite = llvm::make_unique<Module>("llvm-link", getGlobalContext());
    Linker L(*Composite);

    L.linkInModule(move(module));

    unique_ptr<Module> HelperMod(
        parseIRFile(HelperLib, err, getGlobalContext()));
    assert(HelperMod.get() && "Unable to read undo bitcode module");
    L.linkInModule(std::move(HelperMod));

    for (auto &M : ExtractedModules) {
        //errs() << "Linking " << M->getName() << "\n";
        bool ret = L.linkInModule(move(M));
        assert(ret == false && "Error in linkInModule");
    }

    //runAliasEdgeWriter(Composite.get()); 

    common::writeModule(Composite.get(), "full.ll");
    common::generateBinary(*Composite, outFile, optLevel, libPaths, libraries);

    return 0;
}
