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

    std::vector<int> inputSignal;
    std::vector<int> outputSignal;
    std::vector<bool> directFeedthrough;

    std::vector<int> inputWidths;
    int fallbackWidth = 1;

    int xcOffset = 0;
    int xcCount = 0;
    int xdOffset = 0;
    int xdCount = 0;

    int zcOffset = 0;
    int zcCount = 0;

    double sampleTime = kContinuousSampleTime;

    bool isContinuous() const { return sampleTime == kContinuousSampleTime; }
};

struct AlgebraicLoop {
    std::vector<int> blocks;
    std::vector<int> tearSignals;
    int unknowns = 0;
    std::string description;
};

struct ExecutionStep {
    int block = -1;
    int loop = -1;

    bool isLoop() const { return loop >= 0; }
};

struct CompiledModel {
    std::vector<CompiledBlock> blocks;
    std::vector<ExecutionStep> executionOrder;
    std::vector<AlgebraicLoop> loops;
    std::vector<int> signalWidths;
    std::vector<std::string> signalNames;

    int totalContinuousStates = 0;
    int totalDiscreteStates = 0;
    int totalZeroCrossings = 0;

    std::vector<double> sampleTimes;

    bool hasContinuousStates() const { return totalContinuousStates > 0; }

    const CompiledBlock* find(const std::string& blockId) const;
};

class Scheduler {
public:
    static CompiledModel compile(Model& model,
                                 const ExpressionEvaluator& evaluate = {});
};

}
