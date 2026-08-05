#include "Scheduler.h"

#include "model/FlatModel.h"

#include <algorithm>
#include <deque>
#include <map>
#include <queue>
#include <set>

namespace simupy {
namespace {

/// Tarjan's SCC, iterative so a deep diagram cannot overflow the stack.
std::vector<std::vector<int>> stronglyConnectedComponents(
    const std::vector<std::vector<int>>& successors) {
    const int n = static_cast<int>(successors.size());
    std::vector<int> index(n, -1);
    std::vector<int> lowlink(n, 0);
    std::vector<char> onStack(n, 0);
    std::vector<int> stack;
    std::vector<std::vector<int>> components;
    int nextIndex = 0;

    struct Frame {
        int node;
        std::size_t child;
    };
    std::vector<Frame> callStack;

    for (int start = 0; start < n; ++start) {
        if (index[start] != -1) continue;

        index[start] = lowlink[start] = nextIndex++;
        stack.push_back(start);
        onStack[start] = 1;
        callStack.push_back({start, 0});

        while (!callStack.empty()) {
            Frame& frame = callStack.back();
            if (frame.child < successors[frame.node].size()) {
                const int next = successors[frame.node][frame.child++];
                if (index[next] == -1) {
                    index[next] = lowlink[next] = nextIndex++;
                    stack.push_back(next);
                    onStack[next] = 1;
                    callStack.push_back({next, 0});
                } else if (onStack[next]) {
                    lowlink[frame.node] =
                        std::min(lowlink[frame.node], index[next]);
                }
                continue;
            }

            const int node = frame.node;
            callStack.pop_back();
            if (!callStack.empty())
                lowlink[callStack.back().node] =
                    std::min(lowlink[callStack.back().node], lowlink[node]);

            if (lowlink[node] == index[node]) {
                std::vector<int> component;
                int member = -1;
                do {
                    member = stack.back();
                    stack.pop_back();
                    onStack[member] = 0;
                    component.push_back(member);
                } while (member != node);
                components.push_back(std::move(component));
            }
        }
    }
    return components;
}

}

const CompiledBlock* CompiledModel::find(const std::string& blockId) const {
    for (const CompiledBlock& b : blocks)
        if (b.block->id() == blockId) return &b;
    return nullptr;
}

CompiledModel Scheduler::compile(Model& model,
                                 const ExpressionEvaluator& evaluate) {
    const FlatModel flat = flattenModel(model, evaluate);

    CompiledModel compiled;
    compiled.warnings = flat.warnings;
    const int blockCount = static_cast<int>(flat.blocks.size());
    if (blockCount == 0) throw ModelError("the model is empty");

    compiled.blocks.resize(blockCount);
    int signalCount = 0;
    for (int i = 0; i < blockCount; ++i) {
        CompiledBlock& cb = compiled.blocks[i];
        cb.block = flat.blocks[i].block;
        cb.path = flat.blocks[i].path;
        cb.index = i;

        const PortLayout layout = cb.block->ports();
        cb.inputSignal.assign(layout.inputs.size(), -1);
        cb.outputSignal.resize(layout.outputs.size());
        for (std::size_t p = 0; p < layout.outputs.size(); ++p) {
            cb.outputSignal[p] = signalCount++;
            compiled.signalNames.push_back(
                cb.block->signalName(static_cast<int>(p)));
        }
    }

    for (const FlatConnection& c : flat.connections) {
        CompiledBlock& target = compiled.blocks[c.targetBlock];
        const CompiledBlock& source = compiled.blocks[c.sourceBlock];
        // A port that no longer exists, after a parameter changed the count.
        if (c.targetPort >= static_cast<int>(target.inputSignal.size()) ||
            c.sourcePort >= static_cast<int>(source.outputSignal.size())) {
            compiled.warnings.push_back(
                "the wire from '" + source.path + "' port " +
                std::to_string(c.sourcePort + 1) + " to '" + target.path +
                "' port " + std::to_string(c.targetPort + 1) +
                " points at a port that no longer exists: it was dropped and "
                "that input reads zero.");
            continue;
        }
        target.inputSignal[c.targetPort] = source.outputSignal[c.sourcePort];
    }

    compiled.signalWidths.assign(signalCount, 0);

    auto runSetup = [&](CompiledBlock& cb, BlockSetup& setup) {
        const std::size_t inputs = cb.inputSignal.size();
        const std::size_t outputs = cb.outputSignal.size();

        setup.inputWidths.assign(inputs, 0);
        setup.inputConnected.assign(inputs, false);
        for (std::size_t p = 0; p < inputs; ++p) {
            const int signal = cb.inputSignal[p];
            if (signal < 0) continue;
            const int width = compiled.signalWidths[signal];
            if (width <= 0) continue;
            setup.inputWidths[p] = width;
            setup.inputConnected[p] = true;
        }

        setup.outputWidths.assign(outputs, 0);
        setup.directFeedthrough.assign(inputs, true);
        setup.continuousStates = 0;
        setup.discreteStates = 0;
        setup.zeroCrossings = 0;
        setup.sampleTime = kContinuousSampleTime;

        try {
            cb.block->setup(setup);
        } catch (const ModelError& error) {
            throw ModelError("block '" + cb.path + "' (" +
                             cb.block->typeName() + "): " + error.what());
        }
    };

    // Who reads each signal, so a settled width only revisits what it reaches.
    std::vector<std::vector<int>> consumers(signalCount);
    for (const CompiledBlock& cb : compiled.blocks)
        for (int signal : cb.inputSignal)
            if (signal >= 0) consumers[signal].push_back(cb.index);

    std::vector<char> queued(blockCount, 1);
    std::deque<int> pending;
    for (int i = 0; i < blockCount; ++i) pending.push_back(i);

    // A block that keeps changing its mind must not spin for ever.
    long long budget = 8LL * blockCount + 1024;

    while (!pending.empty() && budget-- > 0) {
        const int index = pending.front();
        pending.pop_front();
        queued[index] = 0;

        CompiledBlock& cb = compiled.blocks[index];
        BlockSetup setup;
        runSetup(cb, setup);

        for (std::size_t p = 0; p < cb.outputSignal.size(); ++p) {
            const int width = setup.outputWidths[p];
            const int signal = cb.outputSignal[p];
            if (width <= 0 || compiled.signalWidths[signal] == width) continue;

            compiled.signalWidths[signal] = width;
            for (int consumer : consumers[signal]) {
                if (queued[consumer]) continue;
                queued[consumer] = 1;
                pending.push_back(consumer);
            }
        }
    }

    for (const CompiledBlock& cb : compiled.blocks)
        for (std::size_t p = 0; p < cb.outputSignal.size(); ++p)
            if (compiled.signalWidths[cb.outputSignal[p]] <= 0)
                throw ModelError("could not determine the signal width of '" +
                                 cb.path + "' output " +
                                 std::to_string(p + 1) +
                                 " — set it explicitly on the source block");

    std::set<double> discretePeriods;
    for (CompiledBlock& cb : compiled.blocks) {
        BlockSetup setup;
        runSetup(cb, setup);

        cb.directFeedthrough = setup.directFeedthrough;
        cb.directFeedthrough.resize(cb.inputSignal.size(), true);

        cb.inputWidths = setup.inputWidths;
        cb.fallbackWidth = setup.inheritedWidth();

        cb.xcCount = setup.continuousStates;
        cb.xcOffset = compiled.totalContinuousStates;
        compiled.totalContinuousStates += cb.xcCount;

        cb.xdCount = setup.discreteStates;
        cb.xdOffset = compiled.totalDiscreteStates;
        compiled.totalDiscreteStates += cb.xdCount;

        cb.zcCount = setup.zeroCrossings;
        cb.zcOffset = compiled.totalZeroCrossings;
        compiled.totalZeroCrossings += cb.zcCount;

        cb.sampleTime = setup.sampleTime;
        if (cb.sampleTime > 0.0) discretePeriods.insert(cb.sampleTime);

        if (cb.sampleTime < 0.0)
            throw ModelError("block '" + cb.path +
                             "' declared a negative sample time");
    }
    compiled.sampleTimes.assign(discretePeriods.begin(), discretePeriods.end());

    std::vector<std::vector<int>> successors(blockCount);

    std::map<int, int> producerOf;
    for (const CompiledBlock& cb : compiled.blocks)
        for (int signal : cb.outputSignal) producerOf[signal] = cb.index;

    std::vector<char> selfLoop(blockCount, 0);

    for (const CompiledBlock& cb : compiled.blocks) {
        for (std::size_t p = 0; p < cb.inputSignal.size(); ++p) {
            const int signal = cb.inputSignal[p];
            if (signal < 0 || !cb.directFeedthrough[p]) continue;
            const int producer = producerOf.at(signal);
            if (producer == cb.index) {
                selfLoop[cb.index] = 1;
                continue;
            }
            successors[producer].push_back(cb.index);
        }
    }

    std::vector<std::vector<int>> components =
        stronglyConnectedComponents(successors);
    std::reverse(components.begin(), components.end());

    compiled.executionOrder.reserve(components.size());

    for (std::vector<int>& component : components) {
        const bool isLoop =
            component.size() > 1 || selfLoop[component.front()];

        if (!isLoop) {
            ExecutionStep step;
            step.block = component.front();
            compiled.executionOrder.push_back(step);
            continue;
        }

        std::vector<char> inComponent(blockCount, 0);
        for (int node : component) inComponent[node] = 1;

        std::vector<int> order;
        std::vector<char> visited(blockCount, 0);
        std::vector<std::pair<int, std::size_t>> dfs;

        for (int root : component) {
            if (visited[root]) continue;
            visited[root] = 1;
            dfs.push_back({root, 0});
            while (!dfs.empty()) {
                auto& [node, child] = dfs.back();
                if (child < successors[node].size()) {
                    const int next = successors[node][child++];
                    if (inComponent[next] && !visited[next]) {
                        visited[next] = 1;
                        dfs.push_back({next, 0});
                    }
                    continue;
                }
                order.push_back(node);
                dfs.pop_back();
            }
        }
        std::reverse(order.begin(), order.end());

        std::vector<int> position(blockCount, 0);
        for (std::size_t i = 0; i < order.size(); ++i) position[order[i]] = i;

        AlgebraicLoop loop;
        loop.blocks = order;

        std::set<int> tears;
        for (int node : order) {
            const CompiledBlock& cb = compiled.blocks[node];
            for (std::size_t p = 0; p < cb.inputSignal.size(); ++p) {
                const int signal = cb.inputSignal[p];
                if (signal < 0 || !cb.directFeedthrough[p]) continue;
                const int producer = producerOf.at(signal);
                if (!inComponent[producer]) continue;
                if (position[producer] >= position[node]) tears.insert(signal);
            }
        }

        loop.tearSignals.assign(tears.begin(), tears.end());
        for (int signal : loop.tearSignals)
            loop.unknowns += compiled.signalWidths[signal];

        if (loop.tearSignals.empty())
            throw ModelError("internal error: an algebraic loop was found "
                             "with no edge to tear");

        for (std::size_t i = 0; i < order.size(); ++i) {
            if (i) loop.description += " -> ";
            loop.description += compiled.blocks[order[i]].path;
        }
        loop.description += " -> " + compiled.blocks[order.front()].path;

        ExecutionStep step;
        step.loop = static_cast<int>(compiled.loops.size());
        compiled.loops.push_back(std::move(loop));
        compiled.executionOrder.push_back(step);
    }

    return compiled;
}

}
