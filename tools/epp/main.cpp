#include "llvm/ADT/Triple.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/CodeGen/LinkAllAsmWriterComponents.h"
#include "llvm/CodeGen/LinkAllCodegenComponents.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/Linker.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetLibraryInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Linker/Linker.h"

#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/LoopInfo.h"

#include <memory>
#include <string>

#include "EPPProfile.h"
#include "EPPDecode.h"
#include "AllInliner.h"
#include "Namer.h"
#include "Common.h"

#include "config.h"

#define DEBUG_TYPE "peruse_epp"

using namespace std;
using namespace llvm;
using namespace llvm::sys;
using namespace epp;


cl::opt<string> inPath(cl::Positional, cl::desc("<Module to analyze>"),
                       cl::value_desc("bitcode filename"), cl::Required);

cl::opt<string> outFile("o", cl::desc("Filename of the instrumented program"),
                        cl::value_desc("filename"));

cl::opt<string> profile("p", cl::desc("Path to path profiling results"),
                        cl::value_desc("filename"));

cl::opt<string> selfloop("s",
                         cl::desc("Path to self loop path profiling results"),
                         cl::value_desc("filename"));

cl::opt<unsigned>
    numberOfPaths("n", cl::desc("Number of most frequent paths to compute"),
                  cl::value_desc("number"), cl::init(5));

// Determine optimization level.
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

cl::list<string>
    linkM("b", cl::desc("Bitcode modules to merge (comma separated list)"));

//static void compile(Module &m, string outputPath) {
    //string err;

    //Triple triple = Triple(m.getTargetTriple());
    //const Target *target = TargetRegistry::lookupTarget(MArch, triple, err);
    //if (!target) {
        //report_fatal_error("Unable to find target:\n " + err);
    //}

    //CodeGenOpt::Level level = CodeGenOpt::Default;
    //switch (optLevel) {
    //default:
        //report_fatal_error("Invalid optimization level.\n");
    //// No fall through
    //case '0':
        //level = CodeGenOpt::None;
        //break;
    //case '1':
        //level = CodeGenOpt::Less;
        //break;
    //case '2':
        //level = CodeGenOpt::Default;
        //break;
    //case '3':
        //level = CodeGenOpt::Aggressive;
        //break;
    //}

    //string FeaturesStr;
    //TargetOptions options = InitTargetOptionsFromCodeGenFlags();
    //unique_ptr<TargetMachine> machine(
        //target->createTargetMachine(triple.getTriple(), MCPU, FeaturesStr,
                                    //options, RelocModel, CMModel, level));
    //assert(machine.get() && "Could not allocate target machine!");

    //if (GenerateSoftFloatCalls) {
        //FloatABIForCalls = FloatABI::Soft;
    //}

    //error_code EC;
    //auto out = ::make_unique<tool_output_file>(outputPath.c_str(), EC,
                                               //sys::fs::F_None);
    //if (EC) {
        //report_fatal_error("Unable to create file:\n " + EC.message());
    //}

    //// Build up all of the passes that we want to do to the module.
    //PassManager pm;

    //// Add target specific info and transforms
    //pm.add(new TargetLibraryInfo(triple));
    //machine->addAnalysisPasses(pm);

    //// if (const DataLayout *dl = machine->createDataLayout()) {
    ////      m.setDataLayout(dl);
    //// }
    //pm.add(new DataLayoutPass());

    //{ // Bound this scope
        //formatted_raw_ostream fos(out->os());

        //FileType = TargetMachine::CGFT_ObjectFile;
        //// Ask the target to add backend passes as necessary.
        //if (machine->addPassesToEmitFile(pm, fos, FileType)) {
            //report_fatal_error("target does not support generation "
                               //"of this file type!\n");
        //}

        //// Before executing passes, print the final values of the LLVM options.
        //cl::PrintOptionValues();

        //pm.run(m);
    //}

    //// Keep the output binary if we've been successful to this point.
    //out->keep();
//}

//static void link(const string &objectFile, const string &outputFile) {
    //auto clang = findProgramByName("clang++");
    //if (!clang) {
        //report_fatal_error(
            //"Unable to link output file. Clang not found in PATH.");
    //}

    //string opt("-O");
    //opt += optLevel;

    //vector<string> args{clang.get(), opt, "-o", outputFile, objectFile};

    //for (auto &libPath : libPaths) {
        //args.push_back("-L" + libPath);
    //}

    //for (auto &library : libraries) {
        //args.push_back("-l" + library);
    //}

    //vector<const char *> charArgs;
    //for (auto &arg : args) {
        //charArgs.push_back(arg.c_str());
    //}
    //charArgs.push_back(0);

    //for (auto &arg : args) {
        //DEBUG(outs() << arg.c_str() << " ");
    //}
    //DEBUG(outs() << "\n");

    //string err;
    //if (-1 ==
        //ExecuteAndWait(clang.get(), &charArgs[0], nullptr, 0, 0, 0, &err)) {
        //report_fatal_error("Unable to link output file.");
    //}
//}

//static void generateBinary(Module &m, const string &outputFilename) {
    //// Compiling to native should allow things to keep working even when the
    //// version of clang on the system and the version of LLVM used to compile
    //// the tool don't quite match up.
    //string objectFile = outputFilename + ".o";
    //compile(m, objectFile);
    //link(objectFile, outputFilename);
//}

//static void saveModule(Module &m, StringRef filename) {
    //error_code EC;
    //raw_fd_ostream out(filename.data(), EC, sys::fs::F_None);

    //if (EC) {
        //report_fatal_error("error saving llvm module to '" + filename +
                           //"': \n" + EC.message());
    //}
    //WriteBitcodeToFile(&m, out);
//}

static void instrumentModule(Module &module, std::string, const char *argv0) {
    // Build up all of the passes that we want to run on the module.
    PassManager pm;
    pm.add(new DataLayoutPass());
    pm.add(new llvm::AssumptionCacheTracker());
    pm.add(createLowerSwitchPass());
    pm.add(createLoopSimplifyPass());
    pm.add(createBasicAliasAnalysisPass());
    pm.add(createTypeBasedAliasAnalysisPass());
    pm.add(new LoopInfo());
    pm.add(new llvm::CallGraphWrapperPass());
    //pm.add(new epp::PeruseInliner());
    //pm.add(new epp::Namer());
    pm.add(new epp::EPPProfile());
    pm.add(createVerifierPass());
    pm.run(module);

    // First search the directory of the binary for the library, in case it is
    // all bundled together.
    SmallString<32> invocationPath(argv0);
    sys::path::remove_filename(invocationPath);
    if (!invocationPath.empty()) {
        libPaths.push_back(invocationPath.str());
    }
// If the builder doesn't plan on installing it, we still need to get to the
// runtime library somehow, so just build in the path to the temporary one.
#ifdef CMAKE_INSTALL_PREFIX
    libPaths.push_back(CMAKE_INSTALL_PREFIX "/lib");
#elif defined(CMAKE_TEMP_LIBRARY_PATH)
    libPaths.push_back(CMAKE_TEMP_LIBRARY_PATH);
#endif
    libraries.push_back(RUNTIME_LIB);
    libraries.push_back("rt");
    libraries.push_back("m");

    common::saveModule(module, outFile + ".epp.bc");
    common::generateBinary(module, outFile, optLevel, libPaths, libraries);
}

static void interpretResults(Module &module, std::string filename) {
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
    pm.add(new epp::EPPDecode());
    pm.add(createVerifierPass());
    pm.run(module);
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
    unique_ptr<Module> module = parseIRFile(inPath.getValue(), err, context);

    if (!module.get()) {
        errs() << "Error reading bitcode file.\n";
        err.print(argv[0], errs());
        return -1;
    }

    // Merge all the modules provided in the command line
    Linker LK(module.get());
    for (auto &P : linkM) {
        unique_ptr<Module> L = parseIRFile(P, err, context);
        if (L.get()) {
            if (LK.linkInModule(L.get())) {
                errs() << "Error linking module : " << P << "\n";
            }
        } else {
            errs() << "Error reading bitcode to link : " << P << "\n";
        }
    }

    if (!profile.empty()) {
        interpretResults(*module, profile);
    } else if (!outFile.empty()) {
        instrumentModule(*module, outFile, argv[0]);
    } else {
        errs() << "Neither -o nor -p were selected!\n";
        return -1;
    }

    return 0;
}
