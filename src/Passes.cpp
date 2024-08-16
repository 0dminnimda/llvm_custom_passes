#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

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

template <typename T>
using Array = SmallVector<T>;

namespace {

struct ArgPrintPass : PassInfoMixin<ArgPrintPass> {
    static bool isRequired(void) { return true; }

    auto run(Function &f, FunctionAnalysisManager &) {
        outs() << "\n[ArgPrint]\n";
        outs() << "Function name: " << f.getName() << "\n";
        outs() << "    # of arguments: " << f.arg_size() << "\n";

        return PreservedAnalyses::all();
    }
};

const auto MAX_INSTRUCTIONS = 3;

struct RPOPrintPass : PassInfoMixin<RPOPrintPass> {
    DenseMap<BasicBlock *, u32> block_ids;  // XXX:? Replace by llvm/IR/ValueMap.h
    Array<BasicBlock *> blocks;  // XXX:? llvm/ADT/TinyPtrVector.h

    static bool isRequired(void) { return true; }

    auto index_blocks(Function &f) {
        blocks.clear();
        blocks.resize_for_overwrite(f.size());
        // block_ids.clear();  // This can potentially do memory reallocations, so just leave it as is
        for (auto [id, bb] : enumerate(f)) {
            blocks[id] = &bb;
            block_ids[&bb] = id;
        }
    }

    auto print_indexing() {
        for (auto [id, bb] : enumerate(blocks)) {
            outs() << "Basic block " << id << ": '" << bb->getName() << "'\n";

            auto close_to_end = bb->size() - MAX_INSTRUCTIONS;
            for (auto [i, instr] : enumerate(*bb)) {
                if (i < MAX_INSTRUCTIONS || i >= close_to_end) {
                    outs() << instr << "\n";
                } else if (i == MAX_INSTRUCTIONS) {
                    outs() << "  ..." << "\n";
                }
            }
            outs() << "\n";
        }
    }

    auto calculate_rpo(Function &f, u32 root, Array<u32> &ordering, Array<std::tuple<u32, u32>> &back_edges) {
        typedef enum {
            RPO_NEW,
            RPO_WAIT,
            RPO_SEEN,
            RPO_DONE,
        } RPO_State;

        u64 length = f.size();

        ordering.reserve(length);

        Array<RPO_State> states(length);

        Array<s64> stack;
        /* Large upper bound. Once for all of the nodes,
         * second time for all the post order nodes,
         * and third for possible repeating nodes from loops. */
        stack.reserve(length * 3);

        for(auto &state : states) {
            state = RPO_NEW;
        }
        states[root] = RPO_WAIT;

        stack.push_back((s64)root);

        /* The meat of the iterative reverse post order is
         * to use stack for two kinds of values:
         * - regular visit
         * - post order visit
         * regular visits are what is usually seen in the recutsive approaches.
         * They go through all the successors and visit them.
         * Now the new post order visit is represented as a negative index (actual index - length).
         * It is pushed first thing in the regual visit, and it's purpose
         * is to be visited after it's successors finished the process,
         * after it's guranteed all the nodes that may have come before it were visited. */
        while (stack.size()) {
            s64 id = stack.pop_back_val();

            if (id < 0) {
                /* Post order visit. */
                u32 actual = (u32)(id + length);
                ordering.push_back(actual);
                states[actual] = RPO_DONE;
                continue;
            }

            /* Will be popped after all children are visited
             * thus post order. */
            stack.push_back(id - length);
            states[id] = RPO_SEEN;

            auto term = blocks[id]->getTerminator();
            auto end = term->getNumSuccessors();
            for (u32 i = 0; i < end; ++i) {
                auto child = block_ids[term->getSuccessor(i)];
                RPO_State s = states[child];
                if (s == RPO_WAIT || s == RPO_SEEN) {
                    back_edges.push_back({id, child});
                } else if (s == RPO_NEW) {
                    states[child] = RPO_WAIT;
                    stack.push_back((s64)child);
                }
            }
        }

        std::reverse(std::begin(ordering), std::end(ordering));
    }

    auto run(Function &f, FunctionAnalysisManager &) {
        outs() << "\n[RPOPrint]\n";
        outs() << "Function: " << f.getName() << "\n\n";

        index_blocks(f);

        print_indexing();

        Array<u32> ordering;
        Array<std::tuple<u32, u32>> back_edges;
        calculate_rpo(f, std::distance(f.begin(), f.getEntryBlock().getIterator()), ordering, back_edges);
        outs() << "RPO: ";
        for (auto id : ordering) {
            outs() << id << " ";
        }
        outs() << "\n";
        for (auto [src, dst] : back_edges) {
            outs() << "Back edge:" << dst << "<-" << src << "\n";
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

