#include "LoopFuse.hpp"

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/CodeMoverUtils.h"

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

struct LoopInduction {
    Value *induction_variable;

    Constant *start_const;
    Value *start_variable;

    Constant *stop_const;
    Value *stop_variable;

    Constant *advance_const;
    Value *advance_variable;
    CmpInst::BinaryOps advance_op;
};


struct FusionCandidate {
    Loop *loop;

    BasicBlock *preheader;
    BasicBlock *header;
    BasicBlock *pre_exit;
    BasicBlock *exit;
    BasicBlock *latch;

    LoopInduction induction;

    Array<Value *> writes;
    Array<Value *> reads;
};


bool is_loop_body(FusionCandidate &candidate, BasicBlock *BB) {
    return BB != candidate.header && BB != candidate.latch && BB != candidate.pre_exit;
}


void get_loop_memops(FusionCandidate &candidate) {
    Value *gep_operand;
    bool seen_gep = false;
    bool header_seen_load = false;

    for (BasicBlock *BB : candidate.loop->getBlocks()) {
        if (BB == candidate.header) {
            for (auto &instr : *BB) {
                if (isa<LoadInst>(&instr)) {
                    if (!header_seen_load) {
                        candidate.writes.push_back(instr.getOperand(0));
                        header_seen_load = true;
                        continue;
                    }
                    candidate.reads.push_back(instr.getOperand(0));
                }
            }
        } else if (is_loop_body(candidate, BB)) {
            for (auto &instr : *BB) {
                if (isa<LoadInst>(&instr)) {
                    if (seen_gep) {
                        candidate.reads.push_back(gep_operand);
                        seen_gep = false;
                        continue;
                    }
                    candidate.reads.push_back(instr.getOperand(0));
                }
                if (isa<StoreInst>(&instr)) {
                    if (seen_gep) {
                        candidate.writes.push_back(gep_operand);
                        seen_gep = false;
                        continue;
                    }
                    candidate.writes.push_back(instr.getOperand(1));
                }
                if (isa<GetElementPtrInst>(&instr)) {
                    gep_operand = instr.getOperand(0);
                    seen_gep = true;
                }
            }
        }
    }
}


bool get_loop_induction(FusionCandidate &candidate, DenseMap<Value *, Value *> variables) {
    Value *induction_variable = nullptr;

    Constant *stop_const = nullptr;
    Value *stop_variable = nullptr;

    for (auto &instr : *candidate.header) {
        if (isa<ICmpInst>(&instr)) {
            // dbgs() << "maybe stop " << instr << "\n";
            if (Constant *C = dyn_cast<Constant>(instr.getOperand(1))) {
                // dbgs() << "const \n";
                stop_const = C;
            } else {
                // dbgs() << "maybe var no const" << *instr.getOperand(1) << "\n";
                stop_variable = variables[instr.getOperand(1)];
            }
        } else if (!induction_variable && isa<LoadInst>(&instr)) {
            induction_variable = instr.getOperand(0);
        }
    }

    if (!induction_variable) {
        dbgs() << "Loop does not have an induction variable.\n";
        return false;
    }
    if (!stop_const && !stop_variable) {
        dbgs() << "Loop stop is not a constant or a variable.\n";
        return false;
    }

    bool induction_variable_is_stored = false;
    for (auto *BB : candidate.loop->getBlocks()) {
        if (!is_loop_body(candidate, BB)) continue;

        for (Instruction &instr : *BB) {
            if (!isa<StoreInst>(&instr)) continue;

            if (instr.getOperand(1) == induction_variable) {
                induction_variable_is_stored = true;
            }
        }
    }

    if (!induction_variable_is_stored) {
        dbgs() << "Loop induction variable is not used.\n";
        return false;
    }


    Constant *start_const = nullptr;
    Value *start_variable = nullptr;

    for (auto &instr : *candidate.preheader) {
        if (!isa<StoreInst>(instr)) continue;

        if (ConstantInt *C = dyn_cast<ConstantInt>(instr.getOperand(0))) {
            // Last store value will always be the loop counter start value.
            start_const = C;
        } else {
            start_variable = variables[instr.getOperand(0)];
        }
    }

    if (!start_const && !start_variable) {
        dbgs() << "Loop start is not a constant or a variable.\n";
        return false;
    }


    Constant *advance_const = nullptr;
    Value *advance_variable = nullptr;
    CmpInst::BinaryOps advance_op;

    for (auto &instr : *candidate.latch) {
        if (!isa<BinaryOperator>(instr)) continue;

        advance_op = cast<BinaryOperator>(instr).getOpcode();
        if (ConstantInt *C = dyn_cast<ConstantInt>(instr.getOperand(1))) {
            advance_const = C;
        } else {
            advance_variable = variables[instr.getOperand(1)];
        }
    }

    if (!advance_const && !advance_variable) {
        dbgs() << "Loop advance is not a constant or a variable.\n";
        return false;
    }


    candidate.induction.induction_variable = induction_variable;
    candidate.induction.start_variable = start_variable;
    candidate.induction.start_const = start_const;
    candidate.induction.stop_variable = stop_variable;
    candidate.induction.stop_const = stop_const;
    candidate.induction.advance_variable = advance_variable;
    candidate.induction.advance_const = advance_const;
    candidate.induction.advance_op = advance_op;

    return true;
}


bool create_fusion_candidate(FusionCandidate &candidate, Loop *loop, DenseMap<Value *, Value *> variables) {
    for (auto &BB : loop->getBlocks()) {
        for (auto &Inst : *BB) {
            if (Inst.mayThrow()) {
                dbgs() << "Loop contains instruction that may throw exception.\n";
                return false;
            }
            if (StoreInst *Store = dyn_cast<StoreInst>(&Inst)) {
                if (Store->isVolatile()) {
                    dbgs() << "Loop contains volatile memory access.\n";
                    return false;
                }
            }
            if (LoadInst *Load = dyn_cast<LoadInst>(&Inst)) {
                if (Load->isVolatile()) {
                    dbgs() << "Loop contains volatile memory access.\n";
                    return false;
                }
            }
        }
    }

    /*
    if (!loop->isLoopSimplifyForm()) {
        dbgs() << "Loop is not in simplified form.\n";
        return false;
    }
    */

    candidate.preheader = loop->getLoopPreheader();
    candidate.exit = loop->getUniqueExitBlock();
    if (!candidate.preheader || !candidate.exit) {
        dbgs() << "Loop does not have single entry or exit point.\n";
        return false;
    }

    if (loop->isAnnotatedParallel()) {
        dbgs() << "Loop is annotated parallel.\n";
        return false;
    }

    candidate.header = loop->getHeader();
    candidate.latch = loop->getLoopLatch();
    candidate.pre_exit = loop->getExitingBlock();
    if (!candidate.header || !candidate.latch || !candidate.pre_exit) {
        dbgs() << "Necessary loop information is not available(preheader, header, latch, pre exit, exit block).\n";
        return false;
    }

    candidate.loop = loop;

    get_loop_memops(candidate);

    if (!get_loop_induction(candidate, variables)) {
        return false;
    }

    return true;
}


bool adjacent(FusionCandidate &c1, FusionCandidate &c2) {
    return c1.exit == c2.preheader;
}


bool are_constants_equal(const Constant *LHS, const Constant *RHS) {
    if (LHS == nullptr && RHS == nullptr) {
        return true;
    }

    if (LHS == nullptr || RHS == nullptr) {
        return false;
    }

    if (LHS->getType() != RHS->getType()) {
        return false;
    }

    if (auto *LC = dyn_cast<llvm::ConstantInt>(LHS)) {
        if (auto *RC = dyn_cast<llvm::ConstantInt>(RHS)) {
            return LC->getValue() == RC->getValue();
        }
    } else if (auto *LC = dyn_cast<llvm::ConstantFP>(LHS)) {
        if (auto *RC = dyn_cast<llvm::ConstantFP>(RHS)) {
            return LC->getValueAPF().bitwiseIsEqual(RC->getValueAPF());
        }
    }

    return false;
}


bool same_loop_evolution(FusionCandidate &c1, FusionCandidate &c2) {
    auto &i1 = c1.induction;
    auto &i2 = c2.induction;


    if (i1.stop_const && i2.stop_const) {
        if (!are_constants_equal(i1.stop_const, i2.stop_const)) {
            dbgs() << "Loop stops are not equal\n";
            return false;
        }
    } else if (i1.stop_variable && i2.stop_variable) {
        if (i1.stop_variable != i2.stop_variable) {
            dbgs() << "Loop stops are not equal\n";
            return false;
        }
    } else {
        dbgs() << "Loop stops are not the same kinds of values\n";
        return false;
    }


    if (i1.advance_const && i2.advance_const) {
        if (!are_constants_equal(i1.advance_const, i2.advance_const)) {
            dbgs() << "Loop advances are not equal\n";
            return false;
        }
    } else if (i1.advance_variable && i2.advance_variable) {
        if (i1.advance_variable != i2.advance_variable) {
            dbgs() << "Loop advances are not equal\n";
            return false;
        }
    } else {
        dbgs() << "Loop advances are not the same kinds of values\n";
        return false;
    }


    if (i1.advance_op != i2.advance_op) {
        dbgs() << "Loop advance operations are not the same\n";
        return false;
    }


    if (i1.start_const && i2.start_const) {
        if (!are_constants_equal(i1.start_const, i2.start_const)) {
            dbgs() << "Loop advances are not equal\n";
            return false;
        }
    } else if (i1.start_variable && i2.start_variable) {
        if (i1.start_variable != i2.start_variable) {
            dbgs() << "Loop advances are not equal\n";
            return false;
        }
    } else {
        dbgs() << "Loop starts are not the same kinds of values\n";
        return false;
    }


    return true;
}


bool dependent(FusionCandidate &c1, FusionCandidate &c2) {
    for (auto *v1 : c1.writes) {
        for (auto *v2 : c2.reads) {
            // dbgs() << "VALUE 1: " << V1 << ", VALUE 2: "  << V2 << '\n';
            if (v1 == v2) {
                return true;
            }
        }
        for (auto *v2 : c2.writes) {
            // dbgs() << "VALUE 1: " << V1 << ", VALUE 2: "  << V2 << '\n';
            if (v1 == v2) {
                return true;
            }
        }
    }
    for (auto *v1 : c2.writes) {
        for (auto *v2 : c1.reads) {
            if (v1 == v2) {
                return true;
            }
        }
    }
    return false;
}


bool can_be_fused(FusionCandidate &c1, FusionCandidate &c2) {
    return same_loop_evolution(c1, c2) && !dependent(c1, c2) && adjacent(c1, c2);
}


struct LoopFusionPass : PassInfoMixin<LoopFusionPass> {
    DenseMap<Value *, Value *> variables;

    Function *func;

    LoopAnalysis::Result *LA;
    DominatorTreeAnalysis::Result *DT;
    DependenceAnalysis::Result *DA;
    ScalarEvolutionAnalysis::Result *SE;
    PostDominatorTreeAnalysis::Result *PDT;

    static bool isRequired(void) { return true; }

    void map_variables() {
        for (auto &BB : *func) {
            for (auto &instr : BB) {
                if (isa<LoadInst>(&instr)) {
                    variables[&instr] = instr.getOperand(0);
                }
            }
        }
    }

    auto run(Function &func, FunctionAnalysisManager &AM) {
        this->func = &func;
        LA  = &AM.getResult<LoopAnalysis>(func);
        DT  = &AM.getResult<DominatorTreeAnalysis>(func);
        DA  = &AM.getResult<DependenceAnalysis>(func);
        SE  = &AM.getResult<ScalarEvolutionAnalysis>(func);
        PDT = &AM.getResult<PostDominatorTreeAnalysis>(func);

        map_variables();
        fuse_same_depth_loops_recursive(*LA);

        PreservedAnalyses PA;
        PA.preserve<DominatorTreeAnalysis>();
        PA.preserve<DependenceAnalysis>();
        PA.preserve<ScalarEvolutionAnalysis>();
        PA.preserve<PostDominatorTreeAnalysis>();
        return PA;
    }

    template <typename T>
    void fuse_same_depth_loops_recursive(T &loops) {
        bool collector_has_data = false;
        FusionCandidate collector;

        for (auto &loop : loops) {
            // Nothing is flawless
            fuse_same_depth_loops_recursive(loop->getSubLoops());

            FusionCandidate current;
            if (create_fusion_candidate(current, loop, variables)) {
                dbgs() << "Have a candidate\n";
                if (collector_has_data && can_be_fused(collector, current)) {
                    fuse_with_first(collector, current);
                } else {
                    collector = current;
                }
                collector_has_data = true;
            }
        }
    }

    void fuse_with_first(FusionCandidate &c1, FusionCandidate &c2) {
        moveInstructionsToTheEnd(*c2.preheader, *c1.preheader, *DT, *PDT, *DA);

        c1.pre_exit->getTerminator()->replaceUsesOfWith(c2.preheader, c2.exit);
        c2.preheader->getTerminator()->eraseFromParent();
        new UnreachableInst(c2.preheader->getContext(), c2.preheader);

        c1.latch->getTerminator()->replaceUsesOfWith(c1.header, c2.header);
        c2.latch->getTerminator()->replaceUsesOfWith(c2.header, c1.header);

        DT->recalculate(*func);
        PDT->recalculate(*func);

        LA->removeBlock(c2.preheader);

        DT->recalculate(*func);
        PDT->recalculate(*func);

        moveInstructionsToTheBeginning(*c1.latch, *c2.latch, *DT, *PDT, *DA);
        MergeBlockIntoPredecessor(
            c1.latch->getUniqueSuccessor(), nullptr, LA, nullptr, nullptr, false, DT
        );

        DT->recalculate(*func);
        PDT->recalculate(*func);

        Array<BasicBlock *> Blocks(c2.loop->blocks());
        for (BasicBlock *BB : Blocks) {
            c1.loop->addBlockEntry(BB);
            c2.loop->removeBlockFromLoop(BB);

            // If BB is not a part of c2 that means that it was successfully
            // moved otherwise we need to assign BB to c1 inside-of LoopInfo
            if (LA->getLoopFor(BB) != c2.loop) continue;
            LA->changeLoopFor(BB, c1.loop);
        }

        EliminateUnreachableBlocks(*func);
        LA->erase(c2.loop);

        dbgs() << "Fused\n";
    }
};

}  // namespace

bool register_fuse_pass(StringRef pass_name, FunctionPassManager &FPM, ...) {
    if (pass_name == "LoopFusion") {
        FPM.addPass(LoopFusionPass());
        return true;
    }
    return false;
};
