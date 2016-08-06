#define DEBUG_TYPE "pasha_aew"
#include "MicroWorkloadExtract.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/TypeBasedAliasAnalysis.h"
#include "Statistics.h"

#include "Common.h"
#include "AliasEdgeWriter.h"

using namespace llvm;
using namespace aew;
using namespace std;

void
AliasEdgeWriter::writeEdges(CallInst* CI, Function* OF) {
    // Get all the things we need to check
    // aliasing for
    SetVector<Instruction*> MemOps;
    ReversePostOrderTraversal<Function*> RPOT(OF);
    for (auto BB = RPOT.begin(); BB != RPOT.end(); ++BB) {
        for (auto &I : **BB) {
            if(isa<LoadInst>(&I) || isa<StoreInst>(&I)) {
                if(auto *SI = dyn_cast<StoreInst>(&I)) {
                    if (SI->getMetadata("LO") != nullptr) {
                        continue;
                    }
                }
                // Unlabeled shit will be undo log
                if(I.getMetadata("UID"))
                    MemOps.insert(&I);
            }
        }
    }  
  
    errs() << "MemOps\n";
    for(auto &M : MemOps) {
        errs() << *M << "\n";
    }

    // Setup Arg-Param Map for use with IPAA
    ValueToValueMapTy ArgParamMap;
    uint32_t Idx = 0;
    for(auto &A : OF->args()) {
        ArgParamMap[&A] = CI->getArgOperand(Idx++);
    }
    
    SmallVector<pair<uint32_t, uint32_t>, 16> AliasEdges;
    auto &AA = getAnalysis<AAResultsWrapperPass>(*OF).getAAResults();

    auto getUID = [](Instruction *I) -> uint32_t {
        auto *N = I->getMetadata("UID");
        auto *S = dyn_cast<MDString>(N->getOperand(0));
        return stoi(S->getString().str());
    };

    
    function<Value*(Value *)> getPtr;

    auto getPtrWrapper = [&getPtr] (Value *V) -> Value* {
        if(auto *LI = dyn_cast<LoadInst>(V)) {
            auto *Ptr = LI->getPointerOperand(); 
            return getPtr(Ptr);
        } else if(auto *SI = dyn_cast<StoreInst>(V)) {
            auto *Ptr = SI->getPointerOperand(); 
            return getPtr(Ptr);
        }
    };

    getPtr = [&getPtr, &ArgParamMap](Value *V) -> Value * {
        if(auto *GEP = dyn_cast<GetElementPtrInst>(V)) {
            auto *Ptr = GEP->getPointerOperand();
            getPtr(Ptr);
        } else if(isa<Argument>(V)) {
            getPtr(ArgParamMap[V]);
        }
        return V;
    };

    Data["num-aa-pairs"] = 0;
    Data["num-no-alias"] = 0;
    Data["num-must-alias"] = 0;
    Data["num-partial-alias"] = 0;
    Data["num-may-alias-naive"] = 0;
    Data["num-ld-ld-pairs"] = 0;

    for(auto MB = MemOps.begin(), ME = MemOps.end(); MB != ME; MB++) {
        for(auto NB = next(MB); NB != ME; NB++) {

            Data["num-aa-pairs"]++;

            if(isa<LoadInst>(*MB) && isa<LoadInst>(*NB)) {
                Data["num-ld-ld-pairs"]++;
                continue;
            }

            switch(AA.alias(*MB, *NB)) {
                case AliasResult::NoAlias:
                    Data["num-no-alias"]++;
                    break;
                case AliasResult::MustAlias:
                    Data["num-must-alias"]++;
                    AliasEdges.push_back({getUID(*MB), getUID(*NB)});
                    break;
                case AliasResult::PartialAlias:
                    Data["num-partial-alias"]++;
                    AliasEdges.push_back({getUID(*MB), getUID(*NB)});
                    break;
                case AliasResult::MayAlias: 
                    // Add smarter AA here
                    //errs() << *MB << " may " << *NB << "\n";
                    Data["num-may-alias-naive"]++;
                    AliasEdges.push_back({getUID(*MB), getUID(*NB)});
                    break;
            }
        } 
    }

    ofstream EdgeFile((OF->getName()+".aa.txt").str(), ios::out);
    for(auto P : AliasEdges) {
        EdgeFile << P.first << " " << P.second << "\n";
    }
    EdgeFile.close();
}


bool AliasEdgeWriter::runOnModule(Module &M) {

    DenseMap<StringRef, SmallVector<CallInst*, 1>> Map;

    
    for(auto &F : M) {
        if(F.isDeclaration())
            continue;

        if(F.getName().startswith("__offload_func")) {
            //common::labelUID(F);
        } else {
            for(auto &BB : F) {
                for(auto &I : BB) {
                    if(auto *CI = dyn_cast<CallInst>(&I)) {
                        if(auto *F = CI->getCalledFunction()) {
                            if(F->getName().startswith("__offload_func")) {
                                if(Map.count(F->getName()) == 0) {
                                    Map.insert({F->getName(), SmallVector<CallInst*, 1>()});
                                }
                                Map[F->getName()].push_back(CI);
                            }
                        }
                    }
                }
            }
        }
    }

    assert(Map.size() == 1 && "Only one extracted function at the moment"); 


    for(auto &KV : Map) {
        assert(KV.second.size() == 1 && "Only one call site at the moment");
        for(auto &CI : KV.second) {
            auto *OF = CI->getCalledFunction();
            writeEdges(CI, OF);
        }
    }

    return false;
}

bool AliasEdgeWriter::doInitialization(Module &M) { 
    Data.clear(); 
    return false; 
}

bool AliasEdgeWriter::doFinalization(Module &M) { 
    ofstream Outfile("aew.stats.txt", ios::out);
    for(auto KV: Data) {
        Outfile << KV.first << " " << KV.second << "\n";
    }
    return false; 
}

char AliasEdgeWriter::ID = 0;
