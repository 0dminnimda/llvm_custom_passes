#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <unordered_map>

/* Signed numbers */
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

/* Unsigned numbers */
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* Floating point numbers */
typedef float f32;
typedef double f64;
typedef long double f80;


using namespace llvm;

namespace {

struct ArgPrintPass : PassInfoMixin<ArgPrintPass> {
    static bool isRequired(void) { return true; }

    auto run(Function &f, FunctionAnalysisManager &) {
        outs() << "Function name: " << f.getName() << "\n";
        outs() << "    # of arguments: " << f.arg_size() << "\n";

        return PreservedAnalyses::all();
    }
};

struct RPOPrintPass : PassInfoMixin<RPOPrintPass> {
    std::unordered_map<BasicBlock *, u32> block_ids;

    static bool isRequired(void) { return true; }

    auto index_blocks(Function &f) {
        block_ids.clear();
        u32 id = 0;
        for (auto &bb : f) {
            block_ids[&bb] = id++;
        }
    }

    auto run(Function &f, FunctionAnalysisManager &) {
        outs() << "Function: " << f.getName() << "\n\n";

        index_blocks(f);

        for (auto &bb : f) {
            u64 id = block_ids[&bb];
            outs() << "Basic block " << id << ": " << bb.getName() << "\n";

            for (auto &instr : bb) {
                outs() << instr << "\n";
            }
            outs() << "\n";
        }

        return PreservedAnalyses::all();
    }
};

} /*namespace*/

auto register_passes(StringRef pass_name, FunctionPassManager &FPM, ...) {
    if (pass_name == "ArgPrint") {
        FPM.addPass(ArgPrintPass());
        return true;
    }
    if (pass_name == "RPOPrint") {
        FPM.addPass(RPOPrintPass());
        return true;
    }
    return false;
};

PassPluginLibraryInfo get_plugin_info(void) {
    return {
        LLVM_PLUGIN_API_VERSION,
        "CustomPasses",
        "0.0.0",
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(register_passes);
        }
    };
}

// opt interface
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return get_plugin_info();
}

