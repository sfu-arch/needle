#define DEBUG_TYPE "pasha_common"

#include "llvm/Analysis/Passes.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "Common.h"
#include <fstream>

using namespace llvm;
using namespace helpers;


char DFGPrinter::ID = 0;

bool 
DFGPrinter::doInitialization(Module& M) {
    dot.clear();
    return false;
}

void 
DFGPrinter::visitFunction(Function& F) {
}

string getOpcodeStr(unsigned int N) {
    switch(N) {
#define HANDLE_INST(N, OPCODE, CLASS) \
        case N: \
            return string(#OPCODE); 
#include "llvm/IR/Instruction.def"
        default:
            llvm_unreachable("Unknown Instruction");
    }
}

void 
DFGPrinter::visitBasicBlock(BasicBlock& BB) {

    auto checkCall = [](const Instruction &I, string name) -> bool {
        if(isa<CallInst>(&I) && 
        dyn_cast<CallInst>(&I)->getCalledFunction() &&
        dyn_cast<CallInst>(&I)->getCalledFunction()->getName().startswith(name)) return true;
        return false;
    };

    auto nodeFormat = [](uint64_t id, string label, 
            string color, string ir) -> string {
        stringstream sstr;
        sstr << id << " [label=\""
             << label << "(" << id << ")\", color="
             << color << ",ir=\"" 
             << ir << "\"];\n";
        return sstr.str();
             
    };

    if(nodes.count(&BB) == 0) {
        nodes.insert(make_pair(&BB, counter));
        counter++;
        dot << nodeFormat(nodes[&BB], "BB", "red", BB.getName().str());
    }
    auto BBId = nodes[&BB];
    
    for(auto &I : BB) {
        if(checkCall(I,"llvm.dbg")) continue;

        std::string ir;
        llvm::raw_string_ostream rso(ir);
        I.print(rso); 

        // If this does not exist in the node map
        // then create a new entry for it and save 
        // the value of the counter (identifier).
        if(nodes.count(&I) == 0) {
            nodes.insert(make_pair(&I, counter));
            counter++;
            if(checkCall(I, "__guard_func")) {
                dot << nodeFormat(nodes[&I], "G", "red", rso.str());
            } else {
                dot << nodeFormat(nodes[&I], getOpcodeStr(I.getOpcode()), "black", rso.str());
            } 
        }

        for(auto OI : I.operand_values()) {
            OI->print(rso);
            if(nodes.count(OI) == 0) {
                nodes.insert(make_pair(OI, counter));
                counter++;
                if(isa<Argument>(OI)) {
                    dot << nodeFormat(nodes[OI], "Arg", "blue", rso.str());
                    dot << nodes[OI] << "->" << nodes[&I] << " [color=blue];\n";  
                } else if(isa<Constant>(OI)) {
                    dot << nodeFormat(nodes[OI], "Const", "green", rso.str());
                    dot << nodes[OI] << "->" << nodes[&I] << " [color=green];\n";  
                } else if(isa<BasicBlock>(OI)){
                    dot << nodeFormat(nodes[OI], "BB", "green", BB.getName().str());
                    dot << nodes[&I] << "->" << nodes[OI] << " [color=red];\n";  
                } else {
                    // TODO : This will break later when there are PHINodes
                    // for chops.
                    llvm_unreachable("unexpected");
                }
            } else {
                dot << nodes[OI] << "->" << nodes[&I] << ";\n";  
            }
        }
        // Every Instruction is control depedent on its BB_START
        dot << BBId << "->" << nodes[&I] << " [style=dotted];\n";
    }
}

void 
DFGPrinter::visitInstruction(Instruction& I) {
}

bool 
DFGPrinter::doFinalization(Module& M ) {
    return false;
}

bool 
DFGPrinter::runOnFunction(Function& F ) {
    ofstream dotfile(("dfg."+F.getName()+".dot").str().c_str(), ios::out);
    dot << "digraph G {\n";
    visit(F);
    dot << "}\n";
    dotfile << dot.rdbuf();
    dotfile.close();
    return false;
}
