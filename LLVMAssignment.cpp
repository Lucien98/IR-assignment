//===- Hello.cpp - Example code from "Writing an LLVM Pass" ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements two versions of the LLVM "Hello World" pass described
// in docs/WritingAnLLVMPass.html
//
//===----------------------------------------------------------------------===//

#include <llvm/Support/CommandLine.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/ToolOutputFile.h>

#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Utils.h>

#include <llvm/IR/Function.h>
#include <llvm/Pass.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Argument.h>
#include <llvm/IR/User.h>
#include <llvm/Support/raw_ostream.h>

#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>


using namespace llvm;
static ManagedStatic<LLVMContext> GlobalContext;
static LLVMContext &getGlobalContext() { return *GlobalContext; }
/* In LLVM 5.0, when  -O0 passed to clang , the functions generated with clang will
 * have optnone attribute which would lead to some transform passes disabled, like mem2reg.
 */
struct EnableFunctionOptPass: public FunctionPass {
    static char ID;
    EnableFunctionOptPass():FunctionPass(ID){}
    bool runOnFunction(Function & F) override{
        if(F.hasFnAttribute(Attribute::OptimizeNone))
        {
            F.removeFnAttr(Attribute::OptimizeNone);
        }
        return true;
    }
};

char EnableFunctionOptPass::ID=0;

	
///!TODO TO BE COMPLETED BY YOU FOR ASSIGNMENT 2
///Updated 11/10/2017 by fargo: make all functions
///processed by mem2reg before this pass.
struct FuncPtrPass : public ModulePass {
  static char ID; // Pass identification, replacement for typeid
  FuncPtrPass() : ModulePass(ID) {}

  std::map<int, std::vector<std::string>> results;
  std::vector<std::string> funcNames;

  void printResults(){

      auto ii=results.begin(), ie=results.end();
      do
      {
          auto ji=ii->second.begin(), je=ii->second.end()-1;
          if (ii->second.size()==0) continue;
          for(errs() << ii->first << " : ";
                  ji != je;
                  errs() << *(ji++) << ", ");
          errs() << *(je) << "\n";
      }
      while(++ii != ie);
  }
  void HandleFunction(Function * func)
  {
      for (BasicBlock &bi : *func)
      {
          for (Instruction &ii : bi)
          {
              if (ReturnInst * retinst = dyn_cast<ReturnInst>(&ii))//this line and last line can be merged?
              {
                  HandleObj(retinst->getReturnValue());
              }
          }
      }
  }

  void HandleObj(Value * obj)
  {
      if(auto func = dyn_cast<Function>(obj))
      {
          std::string funcname=func->getName();
          if(find(funcNames.begin(), funcNames.end(), funcname) == funcNames.end())
              funcNames.push_back(funcname);
      }
      else if (auto phinode = dyn_cast<PHINode>(obj))
      {
          for (Value * value: phinode->incoming_values())
              HandleObj(value);
      }
      else if (auto callinst = dyn_cast<CallInst>(obj))
      {
          Function * func = callinst->getCalledFunction();
          if (func)
              HandleFunction(func);
          else
          {
              if (PHINode * phinode = dyn_cast<PHINode>(callinst->getCalledValue()))
                  for (Value * value: phinode->incoming_values())
                      if (Function * func = dyn_cast<Function>(value))
                          HandleFunction(func);
          }
      }
      else if (auto argument = dyn_cast<Argument>(obj))
      {
          Function * func = argument->getParent();
          int arg_index = argument->getArgNo();
          //get the user (call inst as so on) of the function
          for (User *user: func->users())
          {
              if (CallInst * callinst = dyn_cast<CallInst>(user))
              {    //why it is not ==? changed 
                  if (callinst->getCalledFunction() == func) 
                      HandleObj(callinst->getArgOperand(arg_index));
                  else 
                      for (BasicBlock &bi : *(callinst->getCalledFunction()) )
                        for (Instruction &ii : bi)
                          if (ReturnInst *retInst = dyn_cast<ReturnInst>(&ii))
                            if (CallInst *call_inst = dyn_cast<CallInst>(retInst->getReturnValue()))
                              if (Argument *argument = dyn_cast<Argument>(call_inst->getArgOperand(arg_index)))
                                HandleObj(argument);
              }
              else if (PHINode * phinode = dyn_cast<PHINode>(user))
                  //what does it mean by phinode->users
                  for (User * user: phinode->users())
                      if (CallInst * callinst = dyn_cast<CallInst>(user))
                          HandleObj(callinst->getOperand(arg_index));
         }
      }
  }

  bool runOnModule(Module &M) override {
    /*errs().write_escaped(M.getName()) << '\n';
    M.dump();
    errs()<<"------------------------------\n";
    */for (Function  &fi : M)
    {
        if (fi.isIntrinsic()) continue;
        for (BasicBlock &bi : fi)
        {
            for (Instruction &ii : bi)
            {
                if (auto callinst = dyn_cast<CallInst>(&ii))
                {
                      Function * func = callinst->getCalledFunction();                          
                      int line = callinst->getDebugLoc().getLine();                             
                      funcNames.clear();                                                        
                      if (func && func->isIntrinsic()) continue;     
                      HandleObj(callinst->getCalledValue());                                                         
                      !func ? (void)results.insert(std::pair<int, std::vector<std::string>>(line, funcNames))
                            : results.find(line) == results.end()
                              ? (void)results.insert(std::pair<int, std::vector<std::string>>(line, funcNames))
                              : results.find(line)->second.push_back(func->getName());                             
                }
            }
        }
    }
    printResults();
    return false;
  }
};


char FuncPtrPass::ID = 0;
static RegisterPass<FuncPtrPass> X("funcptrpass", "Print function call instruction");

static cl::opt<std::string>
InputFilename(cl::Positional,
              cl::desc("<filename>.bc"),
              cl::init(""));


int main(int argc, char **argv) {
   LLVMContext &Context = getGlobalContext();
   SMDiagnostic Err;
   // Parse the command line to read the Inputfilename
   cl::ParseCommandLineOptions(argc, argv,
                              "FuncPtrPass \n My first LLVM too which does not do much.\n");


   // Load the input module
   std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, Context);
   if (!M) {
      Err.print(argv[0], errs());
      return 1;
   }

   llvm::legacy::PassManager Passes;
   	
   ///Remove functions' optnone attribute in LLVM5.0
   Passes.add(new EnableFunctionOptPass());
   ///Transform it to SSA
   Passes.add(llvm::createPromoteMemoryToRegisterPass());

   /// Your pass to print Function and Call Instructions
   Passes.add(new FuncPtrPass());
   Passes.run(*M.get());
}

