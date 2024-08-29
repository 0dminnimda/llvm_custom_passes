#include "llvm/Passes/PassBuilder.h"

bool register_fuse_pass(llvm::StringRef pass_name, llvm::FunctionPassManager &FPM, ...);
