#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <map>
#include <set>
#include <string>

using namespace llvm;
using namespace std;

struct Count : public ModulePass {
    static char ID;

    // Count Methods

    Count() : ModulePass(ID) {}

    map<unsigned, unsigned long long> CountMap;
    uint64_t Counter = 0;
    uint64_t BBCounter = 0;

    set<pair<Value *, Value *>> EdgeMap;

    bool doInitialization(Module &M) {
        // assert(FunctionList.size() == 1 &&
        //"Can only patch one function at a time");
        CountMap[Instruction::Add] = 0;
        CountMap[Instruction::FAdd] = 0;
        CountMap[Instruction::GetElementPtr] = 0;
        CountMap[Instruction::Load] = 0;
        return false;
    }

    bool doFinalization(Module &M) {
        errs() << "TotalOps: " << Counter << "\n";
        errs() << "TotalBlocks: " << BBCounter << "\n";
        errs() << "TotalUniqueTypes: " << CountMap.size() << "\n";
        for (auto KV : CountMap) {
            errs() << opcodeToStr(KV.first) << " " << KV.second << "\n";
        }
        errs() << "Edges: " << EdgeMap.size() << "\n";
        return false;
    }

    bool runOnModule(Module &M) {
        for (auto &F : M) {
            // errs() << F.getName() << "\n";
            // if(isTargetFunction(F, FunctionList)) {
            if (F.isDeclaration())
                continue;
            for (auto &BB : F) {
                BBCounter++;
                // errs() << BB.getName() << "\n";
                for (auto &I : BB) {
                    CountMap[opcodeBucket(I.getOpcode())] += 1;
                    Counter++;

                    for (auto ob = I.op_begin(), oe = I.op_end(); ob != oe;
                         ob++) {
                        EdgeMap.insert({ob->get(), &I});
                    }

                    if (auto *SI = dyn_cast<StoreInst>(&I)) {
                        if (auto *BI = dyn_cast<BitCastInst>(
                                SI->getPointerOperand())) {
                            if (auto *GI = dyn_cast<GetElementPtrInst>(
                                    BI->getOperand(0))) {
                                if (GI->getPointerOperand()
                                        ->getName()
                                        .startswith("__live_out")) {
                                    CountMap[Instruction::GetElementPtr] -= 1;
                                    CountMap[Instruction::BitCast] -= 1;
                                    Counter -= 2;
                                }
                            }
                        }
                    }
                    // if(auto *CI = dyn_cast<CallInst>(&I)) {
                    // if(CI->getCalledFunction().getName().startsWith("llvm.lifetime"))
                    // {
                    // CountMap[Instruction::BitCast] -= 1;
                    // CountMap[Instruction::Call] -= 1;
                    // Counter -= 2;
                    //}
                    //}
                }
            }
            //}
        }
        return true;
    }

    void releaseMemory() {
        // Counter = 0;
        // CountMap.clear();
    }

    void getAnalysisUsage(AnalysisUsage &AU) const {}

    unsigned opcodeBucket(unsigned opCode) {
        switch (opCode) {
        // Terminators
        case Instruction::Ret:
        case Instruction::Br:
        case Instruction::Switch:
        case Instruction::IndirectBr:
        case Instruction::Select:
            return Instruction::Ret;

        // Standard binary operators...
        case Instruction::Add:
        case Instruction::Sub:
        case Instruction::Mul:
        case Instruction::UDiv:
        case Instruction::SDiv:
        case Instruction::URem:
        case Instruction::SRem:
        case Instruction::ICmp:
        case Instruction::FCmp:
        case Instruction::Shl:
        case Instruction::LShr:
        case Instruction::AShr:
        case Instruction::And:
        case Instruction::Or:
        case Instruction::Xor:
            return Instruction::Add;

        case Instruction::FAdd:
        case Instruction::FRem:
        case Instruction::FDiv:
        case Instruction::FMul:
        case Instruction::FSub:
            return Instruction::FAdd;

        // Memory instructions...
        case Instruction::Load:
        case Instruction::Store:
        case Instruction::AtomicCmpXchg:
        case Instruction::AtomicRMW:
        case Instruction::Fence:
            return Instruction::Load;

        case Instruction::GetElementPtr:
            return Instruction::GetElementPtr;

        case Instruction::PHI:
        case Instruction::Call:
        case Instruction::Invoke:

        // Convert instructions...
        case Instruction::Alloca:
        case Instruction::Trunc:
        case Instruction::FPTrunc:
        case Instruction::SExt:

        case Instruction::ZExt:
        case Instruction::FPExt:
        case Instruction::FPToUI:
        case Instruction::FPToSI:
        case Instruction::UIToFP:
        case Instruction::SIToFP:
        case Instruction::IntToPtr:
        case Instruction::PtrToInt:
        case Instruction::BitCast:
        case Instruction::AddrSpaceCast:

        // Other instructions...
        case Instruction::ExtractElement:
        case Instruction::InsertElement:
        case Instruction::ShuffleVector:
        case Instruction::ExtractValue:
        case Instruction::InsertValue:
        case Instruction::VAArg:
        case Instruction::LandingPad:
        case Instruction::Resume:
        case Instruction::Unreachable:
        default:
            return Instruction::Ret;
        }
    }

    string opcodeToStr(unsigned opCode) {
        switch (opCode) {
        // Terminators
        case Instruction::Ret:
            return "Ret";
        case Instruction::Br:
            return "Br";
        case Instruction::Switch:
            return "Switch";
        case Instruction::IndirectBr:
            return "IndirectBr";

        case Instruction::Select:
            return "Select";

        // Standard binary operators...
        case Instruction::Add:
            return "Add";
        case Instruction::Sub:
            return "Sub";
        case Instruction::Mul:
            return "Mul";
        case Instruction::UDiv:
            return "UDiv";
        case Instruction::SDiv:
            return "SDiv";
        case Instruction::URem:
            return "URem";
        case Instruction::SRem:
            return "SRem";
        case Instruction::ICmp:
            return "ICmp";
        case Instruction::FCmp:
            return "FCmp";
        case Instruction::Shl:
            return "Shl";
        case Instruction::LShr:
            return "LShr";
        case Instruction::AShr:
            return "AShr";
        // Logical operators...
        case Instruction::And:
            return "And";
        case Instruction::Or:
            return "Or ";
        case Instruction::Xor:
            return "Xor";

        case Instruction::FAdd:
            return "FAdd";
        case Instruction::FRem:
            return "FRem";
        case Instruction::FDiv:
            return "FDiv";
        case Instruction::FMul:
            return "FMul";
        case Instruction::FSub:
            return "FSub";

        // Memory instructions...
        case Instruction::Alloca:
            return "Alloca";
        case Instruction::Load:
            return "Load";
        case Instruction::Store:
            return "Store";
        case Instruction::AtomicCmpXchg:
            return "AtomicCmpXchg";
        case Instruction::AtomicRMW:
            return "AtomicRMW";
        case Instruction::Fence:
            return "Fence";

        case Instruction::GetElementPtr:
            return "GetElementPtr";

        case Instruction::PHI:
            return "PHI";

        case Instruction::Call:
            return "Call";
        case Instruction::Invoke:
            return "Invoke";

        // Convert instructions...
        case Instruction::Trunc:
            return "Trunc";
        case Instruction::FPTrunc:
            return "FPTrunc";
        case Instruction::SExt:
            return "SExt";

        case Instruction::ZExt:
            return "ZExt";
        case Instruction::FPExt:
            return "FPExt";
        case Instruction::FPToUI:
            return "FPToUI";
        case Instruction::FPToSI:
            return "FPToSI";
        case Instruction::UIToFP:
            return "UIToFP";
        case Instruction::SIToFP:
            return "SIToFP";
        case Instruction::IntToPtr:
            return "IntToPtr";
        case Instruction::PtrToInt:
            return "PtrToInt";
        case Instruction::BitCast:
            return "BitCast";
        case Instruction::AddrSpaceCast:
            return "AddrSpaceCast";

        // Other instructions...
        case Instruction::ExtractElement:
            return "ExtractElement";
        case Instruction::InsertElement:
            return "InsertElement";
        case Instruction::ShuffleVector:
            return "ShuffleVector";
        case Instruction::ExtractValue:
            return "ExtractValue";
        case Instruction::InsertValue:
            return "InsertValue";
        case Instruction::VAArg:
            return "VAArg";
        case Instruction::LandingPad:
            return "LandingPad";
        case Instruction::Resume:
            return "Resume";
        case Instruction::Unreachable:
            return "Unreachable";
        default:
            return "Unknown";
        }
    }
};

char Count::ID = 0;
static RegisterPass<Count> X("pasha-count-ins",
                             "Count instances of different instruciton types");
