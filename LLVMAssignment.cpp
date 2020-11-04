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

  void Push(std::string funcname)
  {
      if(find(funcNames.begin(), funcNames.end(), funcname) == funcNames.end())
      {
          funcNames.push_back(funcname);
      }
  }
  void printResults(){
      for (auto ii=results.begin(), ie=results.end(); ii!=ie; ii++)
      {
          //why there is a line with no function called insert into the funcNames vector
          errs() << ii->first << ": ";
          if (ii->second.size() == 0){ errs() << "\n"; continue;}
          for (auto ji=ii->second.begin(), je=ii->second.end()-1; ji!=je; ji++)//why substract 1
          {
              if (ii->second.size()>0)
              errs() << *ji << ", ";
          }
          errs() << *(ii->second.end()-1) << "\n";
      }
  }
  void HandleObj(Value * obj)
  {
      if(auto func = dyn_cast<Function>(obj))
      {
          Push(func->getName());
      }
      else if (auto callinst = dyn_cast<CallInst>(obj))
      {
          Value * value = callinst->getCalledValue();
          HandleObj(value);
      }
      else if (auto phinode = dyn_cast<PHINode>(obj))
      {
          for (Value * value: phinode->incoming_values())
          {
              HandleObj(value);
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
              {
                  //why it is not ==? changed 
                  Value * value = callinst->getArgOperand(arg_index);
                  if (arg_index <= callinst->getNumOperands()-1)
                  {
                      if (callinst->getCalledFunction() == func) {HandleObj(value);}
                      else { // 递归问题
                          Function *func = callinst->getCalledFunction();
                          for (Function::iterator bi = func->begin(), be = func->end(); bi != be; bi++)
                          {
                            // for instruction in basicblock
                            for (BasicBlock::iterator ii = bi->begin(), ie = bi->end(); ii != ie; ii++)
                            {
                              Instruction *inst = dyn_cast<Instruction>(ii);
                              if (ReturnInst *retInst = dyn_cast<ReturnInst>(inst))
                              {
                                Value *v = retInst->getReturnValue();
                                if (CallInst *call_inst = dyn_cast<CallInst>(v))
                                {
                                  Value *value = call_inst->getArgOperand(arg_index);
                                  if (Argument *argument = dyn_cast<Argument>(value))
                                  {
                                    HandleObj(argument);
                                  }
                                }
                              }
                            }
                          }
                        }
                      }
                  }
              else if (PHINode * phinode = dyn_cast<PHINode>(user))
              {
                  //what does it mean by phinode->users
                  for (User * user: phinode->users())
                  {
                      if (CallInst * callinst = dyn_cast<CallInst>(user))
                      {
                          //if(arg_index < callinst->getNumArgOperands())
                          {
                              Value * value = callinst->getArgOperand(arg_index);
                              HandleObj(value);
                          }
                      }
                  }
              }
          }

        }
  }
  void GetResults(CallInst * callinst)
  {
      //get the function and line it locates in a CallInst
      Function * func = callinst->getCalledFunction();
      int line = callinst->getDebugLoc().getLine();
      funcNames.clear();
      if(func)
      {
          //push the function name and line number into the results map
          std::string funcname = func->getName();
          if (funcname != std::string("llvm.dbg.value"))
          {
              Push(funcname);
              if (results.find(line) == results.end())
              {
                  results.insert(std::pair<int, std::vector<std::string>>(line, funcNames));
              }
              else
              {
                  auto i = results.find(line);
                  i->second.push_back(funcname);
              }
          }
      }
      else
      {
          Value *value = callinst->getCalledValue();
          HandleObj(value);
          //what if the funct is not in a phinode
          results.insert(std::pair<int, std::vector<std::string>>(line, funcNames));
      }

  }

  bool runOnModule(Module &M) override {
    /*errs() << "Hello: ";
    errs().write_escaped(M.getName()) << '\n';
    M.dump();
    errs()<<"------------------------------\n";
    */for (Module::iterator fi=M.begin(), fe=M.end(); fi!=fe; fi++)
    {
        for (Function::iterator bi=fi->begin(), be=fi->end(); bi!=be; bi++)
        {
            for (BasicBlock::iterator ii=bi->begin(), ie=bi->end(); ii!=ie; ii++)
            {
                Instruction * inst = dyn_cast<Instruction>(ii);
                if (auto callinst = dyn_cast<CallInst>(inst))
                {
                    GetResults(callinst);
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

