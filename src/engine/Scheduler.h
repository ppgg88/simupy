#pragma once

#include "model/FlatModel.h"
#include "model/Model.h"

#include <string>
#include <vector>

namespace simupy {

struct CompiledBlock {
    Block* block = nullptr;
    int index = 0;
    std::string path;

    std::vector<int> inputSignal;   ///< signal id per input port, -1 if dangling
    std::vector<int> outputSignal;  ///< signal id per output port
    std::vector<bool> directFeedthrough;

    std::vector<int> inputWidths;  ///< resolved width per input, 0 if dangling
    /// Width used for the zero-filled buffer handed to dangling inputs.
    int fallbackWidth = 1;

    int xcOffset = 0;  ///< slice into the global continuous state vector
    int xcCount = 0;
    int xdOffset = 0;  ///< slice into the global discrete state vector
    int xdCount = 0;

    int zcOffset = 0;  ///< slice into the global zero-crossing vector
    int zcCount = 0;

    double sampleTime = kContinuousSampleTime;

    bool isContinuous() const { return sampleTime == kContinuousSampleTime; }
};

/// A set of blocks that depend on each other through direct feedthrough, so
/// no evaluation order alone can satisfy them.
///
/// Solving one means finding values for the *torn* signals — the ones on the
/// edges that close the cycle — such that re-evaluating the group reproduces
/// them. The Simulator does that with Newton iteration.
/// Blocks that depend on each other through direct feedthrough. Solved by
/// Newton iteration on the torn signals rather than refused.
struct AlgebraicLoop {
    std::vector<int> blocks;       ///< evaluation order inside the loop
    std::vector<int> tearSignals;  ///< signals assumed at the start of a pass
    int unknowns = 0;              ///< total width of the torn signals
    std::string description;       ///< "A -> B -> C -> A", for diagnostics
};

struct ExecutionStep {
    int block = -1;  ///< index into CompiledModel::blocks, or -1
    int loop = -1;   ///< index into CompiledModel::loops, or -1

    bool isLoop() const { return loop >= 0; }
};

struct CompiledModel {
    std::vector<CompiledBlock> blocks;
    std::vector<ExecutionStep> executionOrder;
    std::vector<AlgebraicLoop> loops;
    std::vector<int> signalWidths;  ///< width of every signal id
    std::vector<std::string> signalNames;

    int totalContinuousStates = 0;
    int totalDiscreteStates = 0;
    int totalZeroCrossings = 0;

    std::vector<double> sampleTimes;

    bool hasContinuousStates() const { return totalContinuousStates > 0; }

    const CompiledBlock* find(const std::string& blockId) const;
};

/// Turns a Model into a CompiledModel.
///
/// Runs three passes:
///   1. signal width propagation to a fixpoint (so that inherited widths
///      settle even through feedback loops),
///   2. a final setup pass recording state counts, sample times, zero
///      crossings and feedthrough flags,
///   3. a strongly-connected-component decomposition of the direct-feedthrough
///      graph. Components of one block become plain steps in topological
///      order; larger ones become algebraic loops with their closing edges
///      torn, for the Simulator to solve.
///
/// Throws ModelError with a human-readable message on any failure.
class Scheduler {
public:
    static CompiledModel compile(Model& model,
                                 const ExpressionEvaluator& evaluate = {});
};

}  // namespace simupy
