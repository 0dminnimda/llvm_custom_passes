#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

struct ArgPrintPass : PassInfoMixin<ArgPrintPass> {
    static bool isRequired(void) { return true; }

    auto run(Function &f, FunctionAnalysisManager &) {
        errs() << "Function name: " << f.getName() << "\n";
        errs() << "    # of arguments: " << f.arg_size() << "\n";

        return PreservedAnalyses::all();
    }
};

} /*namespace*/

auto register_arg_print(StringRef Name, FunctionPassManager &FPM, ...) {
    if (Name == "ArgPrintPass") {
        FPM.addPass(ArgPrintPass());
        return true;
    }
    return false;
};

PassPluginLibraryInfo get_plugin_info(void) {
    return {
        /* APIversion = */ LLVM_PLUGIN_API_VERSION,
        /* PluginName = */ "CustomPasses",
        /* PluginVersion = */ LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(register_arg_print);
        }
    };
}

// opt interface
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return get_plugin_info();
}

