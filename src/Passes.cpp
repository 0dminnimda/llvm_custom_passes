#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include "LoopFuse.hpp"

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

    auto run(Function &func, FunctionAnalysisManager &) {
        dbgs() << "\n[ArgPrint]\n";
        dbgs() << "Function name: " << func.getName() << "\n";
        dbgs() << "    # of arguments: " << func.arg_size() << "\n";

        return PreservedAnalyses::all();
    }
};

const auto MAX_INSTRUCTIONS = 3;

struct RPOPrintPass : PassInfoMixin<RPOPrintPass> {
    DenseMap<BasicBlock *, u32> block_ids;  // XXX:? Replace by llvm/IR/ValueMap.h
    Array<BasicBlock *> blocks;  // XXX:? llvm/ADT/TinyPtrVector.h

    static bool isRequired(void) { return true; }

    auto index_blocks(Function &func) {
        blocks.clear();
        blocks.resize_for_overwrite(func.size());
        // block_ids.clear();  // This can potentially do memory reallocations, so just leave it as is
        for (auto [id, bb] : enumerate(func)) {
            blocks[id] = &bb;
            block_ids[&bb] = id;
        }
    }

    auto print_indexing() {
        for (auto [id, bb] : enumerate(blocks)) {
            dbgs() << "Basic block " << id << ": '" << bb->getName() << "'\n";

            auto close_to_end = bb->size() - MAX_INSTRUCTIONS;
            for (auto [i, instr] : enumerate(*bb)) {
                if (i < MAX_INSTRUCTIONS || i >= close_to_end) {
                    dbgs() << instr << "\n";
                } else if (i == MAX_INSTRUCTIONS) {
                    dbgs() << "  ..." << "\n";
                }
            }
            dbgs() << "\n";
        }
    }

    auto calculate_rpo(Function &func, u32 root, Array<u32> &ordering, Array<std::tuple<u32, u32>> &back_edges) {
        typedef enum {
            RPO_NEW,
            RPO_WAIT,
            RPO_SEEN,
            RPO_DONE,
        } RPO_State;

        u64 length = func.size();

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

    auto run(Function &func, FunctionAnalysisManager &) {
        dbgs() << "\n[RPOPrint]\n";
        dbgs() << "Function: " << func.getName() << "\n\n";

        index_blocks(func);

        print_indexing();

        Array<u32> ordering;
        Array<std::tuple<u32, u32>> back_edges;
        calculate_rpo(func, std::distance(func.begin(), func.getEntryBlock().getIterator()), ordering, back_edges);
        dbgs() << "RPO: ";
        for (auto id : ordering) {
            dbgs() << id << " ";
        }
        dbgs() << "\n";
        for (auto [src, dst] : back_edges) {
            dbgs() << "Back edge:" << dst << "<-" << src << "\n";
        }

        return PreservedAnalyses::all();
    }
};

struct InstructionCounterPass : PassInfoMixin<InstructionCounterPass> {
    StringMap<u32> counts;

    static bool isRequired(void) { return true; }

    auto count(Function &func) {
        counts.clear();
        for (auto &bb : func) {
            for (auto &instr : bb) {
                counts[instr.getOpcodeName()] += 1;
            }
        }
    }

    auto print() {
        for (auto &[name, count] : counts) {
            dbgs() << "  " << name << ": " << count << "\n";
        }
    }

    auto run(Function &func, FunctionAnalysisManager &) {
        dbgs() << "\n[InstrCount]\n";
        dbgs() << "Function " << func.getName() << "():\n";

        count(func);
        print();

        return PreservedAnalyses::all();
    }
};


struct TripCountPass : PassInfoMixin<TripCountPass> {
    static bool isRequired(void) { return true; }

    auto run(Function &func, FunctionAnalysisManager &AM) {
        dbgs() << "\n[TripCount]\n";
        dbgs() << "Function " << func.getName() << "():\n";

        auto &SE = AM.getResult<ScalarEvolutionAnalysis>(func);
        auto &LA = AM.getResult<LoopAnalysis>(func);

        for (const Loop *loop : LA) {
            // auto *header = loop->getHeader();
            const SCEV *trip_count = SE.getBackedgeTakenCount(loop);
            trip_count->print(dbgs());
            dbgs() << "\n";
            if (const auto *C = dyn_cast<const SCEVConstant>(trip_count)) {
                auto count = C->getValue()->getZExtValue();
                dbgs() << "Loop at " << loop->getName() << "': Trip count = " << count << "\n";
            } else {
                dbgs() << "Loop at " << loop->getName() << "': Unable to compute trip count\n";
            }
        }

        return PreservedAnalyses::all();
    }
};

struct InductionsPass : PassInfoMixin<InductionsPass> {
    static bool isRequired(void) { return true; }

    auto run(Function &func, FunctionAnalysisManager &AM) {
        dbgs() << "\n[Inductions]\n";
        dbgs() << "Function " << func.getName() << "():\n";

        auto &SE = AM.getResult<ScalarEvolutionAnalysis>(func);
        auto &LA = AM.getResult<LoopAnalysis>(func);

        for (const Loop *loop : LA) {
            // loop->setLoopPreheader();
            dbgs() << "Loop at " << *loop->getHeader()->getFirstNonPHI() << ":\n";

            for (PHINode &phi : loop->getHeader()->phis()) {
                // Check if the PHI node is an induction variable.
                if (!(SE.isSCEVable(phi.getType()) && SE.getSCEV(&phi)->getSCEVType() == scAddRecExpr)) continue;

                const SCEVAddRecExpr *AR = cast<SCEVAddRecExpr>(SE.getSCEV(&phi));

                dbgs() << "  Induction variable: " << phi << "\n";

                // Get the start value of the induction variable.
                const SCEV *Start = AR->getStart();
                dbgs() << "    Start: " << *Start << " = ";
                if (auto *ConstStart = dyn_cast<SCEVConstant>(Start)) {
                  dbgs() << ConstStart->getValue()->getSExtValue() << "\n";
                } else {
                  dbgs() << "Not a constant\n";
                }

                // Get the step value of the induction variable.
                const SCEV *Step = AR->getStepRecurrence(SE);
                dbgs() << "    Step: " << *Step << " = ";
                if (auto *ConstStep = dyn_cast<SCEVConstant>(Step)) {
                  dbgs() << ConstStep->getValue()->getSExtValue() << "\n";
                } else {
                  dbgs() << "Not a constant\n";
                }

                // You can also get the trip count of the loop if it's known:
                if (const SCEVConstant *TripCount = dyn_cast_or_null<SCEVConstant>(SE.getBackedgeTakenCount(loop))) {
                  dbgs() << "    Trip count: " << TripCount->getValue()->getSExtValue() << "\n";
                } else {
                  dbgs() << "    Trip count: Unknown\n";
                }
            }
        }

        return PreservedAnalyses::all();
    }
};


struct LoopPass : PassInfoMixin<LoopPass> {
    static bool isRequired(void) { return true; }

    auto run(Function &func, FunctionAnalysisManager &AM) {
        dbgs() << "\n[Loop]\n";
        dbgs() << "Function " << func.getName() << "():\n";

        auto &SE = AM.getResult<ScalarEvolutionAnalysis>(func);
        auto &LA = AM.getResult<LoopAnalysis>(func);

        for (Loop *loop : LA) {
            printLoopHierarchy(loop, 0, SE);
        }

        return PreservedAnalyses::all();
    }

    void printLoopHierarchy(Loop *loop, int depth, ScalarEvolution &SE) {
        dbgs().indent(depth * 2) << "<loop at depth " << depth;

        InductionDescriptor induction;
        if (loop->getInductionDescriptor(SE, induction)) {
            dbgs() << "; induction = " << induction.getStep();
        } else {
            dbgs() << "; induction is unknown";
        }

        PHINode *induction_var = loop->getInductionVariable(SE);
        if (induction_var) {
            dbgs() << "; induction_var" << *induction_var;
        } else {
            dbgs() << "; no induction_var";
        }

        auto bounds = loop->getBounds(SE);

        if (bounds) {
            dbgs() << "; yes bounds";
        } else {
            dbgs() << "; no bounds";
        }

        dbgs() << "> {\n";

        // bool isLoopSimplifyForm() const;

        for (Loop *sub_loop : loop->getSubLoops()) {
            printLoopHierarchy(sub_loop, depth + 1, SE);
        }

        dbgs().indent(depth * 2) << "}\n";
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
    if (pass_name == "InstrCount") {
        FPM.addPass(InstructionCounterPass());
        return true;
    }
    if (pass_name == "TripCount") {
        FPM.addPass(TripCountPass());
        return true;
    }
    if (pass_name == "Inductions") {
        FPM.addPass(InductionsPass());
        return true;
    }
    if (pass_name == "Loop") {
        FPM.addPass(LoopPass());
        return true;
    }
    return false;
};

PassPluginLibraryInfo get_plugin_info(void) {
    return {
        LLVM_PLUGIN_API_VERSION,
        "CustomPasses",
        "v0.1",
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(register_passes);
            PB.registerPipelineParsingCallback(register_fuse_pass);
        }
    };
}

// opt interface
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return get_plugin_info();
}

