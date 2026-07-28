#include "BlockUtils.h"

#include <algorithm>

namespace simupy {
namespace {

class MuxBlock : public Block {
public:
    PortLayout ports() const override {
        return {numberedPorts("in", std::max(1, params().integer("inputs", 2))),
                {"out"}};
    }

    void setup(BlockSetup& s) override {
        offsets_.clear();
        int total = 0;
        for (int i = 0; i < s.inputCount(); ++i) {
            offsets_.push_back(total);
            total += s.inputConnected[i] ? s.inputWidths[i] : 1;
        }
        s.outputWidths[0] = std::max(1, total);
    }

    void computeOutputs(const EvalContext& c) override {
        Vec& y = c.out(0);
        y.setZero();
        for (int i = 0; i < c.nu && i < static_cast<int>(offsets_.size()); ++i) {
            const Vec& u = c.in(i);
            const int offset = offsets_[i];
            for (Eigen::Index k = 0; k < u.size(); ++k) {
                const Eigen::Index target = offset + k;
                if (target < y.size()) y[target] = u[k];
            }
        }
    }

private:
    std::vector<int> offsets_;
};

class DemuxBlock : public Block {
public:
    PortLayout ports() const override {
        return {{"in"},
                numberedPorts("out", std::max(1, params().integer("outputs", 2)))};
    }

    void setup(BlockSetup& s) override {
        const int outputs = s.outputCount();
        const int total = s.inputConnected[0] ? s.inputWidths[0] : outputs;

        const std::vector<double> requested = params().vector("widths", {});
        widths_.assign(outputs, 0);

        if (!requested.empty() && requested.size() != 1) {
            if (static_cast<int>(requested.size()) != outputs)
                throw ModelError("the width list has " +
                                 std::to_string(requested.size()) +
                                 " entries but the block has " +
                                 std::to_string(outputs) + " outputs");
            int sum = 0;
            for (int i = 0; i < outputs; ++i) {
                widths_[i] = std::max(1, static_cast<int>(requested[i]));
                sum += widths_[i];
            }
            if (s.inputConnected[0] && sum != total)
                throw ModelError("the widths sum to " + std::to_string(sum) +
                                 " but the input signal is " +
                                 std::to_string(total) + " wide");
        } else {
            const int base = total / outputs;
            const int remainder = total % outputs;
            for (int i = 0; i < outputs; ++i)
                widths_[i] = std::max(1, base + (i < remainder ? 1 : 0));
        }

        offsets_.assign(outputs, 0);
        int offset = 0;
        for (int i = 0; i < outputs; ++i) {
            offsets_[i] = offset;
            offset += widths_[i];
            s.outputWidths[i] = widths_[i];
        }
    }

    void computeOutputs(const EvalContext& c) override {
        const Vec& u = c.in(0);
        for (int i = 0; i < c.ny; ++i) {
            Vec& y = c.out(i);
            for (Eigen::Index k = 0; k < y.size(); ++k) {
                const Eigen::Index source = offsets_[i] + k;
                y[k] = source < u.size() ? u[source] : 0.0;
            }
        }
    }

private:
    std::vector<int> widths_, offsets_;
};

class SelectorBlock : public Block {
public:
    PortLayout ports() const override { return {{"in"}, {"out"}}; }

    void setup(BlockSetup& s) override {
        const std::vector<double> indices = params().vector("indices", {0.0});
        if (indices.empty())
            throw ModelError("at least one index must be selected");

        indices_.clear();
        for (double index : indices) {
            const int value = static_cast<int>(index);
            if (value < 0) throw ModelError("indices are zero-based and cannot "
                                            "be negative");
            indices_.push_back(value);
        }

        if (s.inputConnected[0]) {
            const int width = s.inputWidths[0];
            for (int index : indices_)
                if (index >= width)
                    throw ModelError("index " + std::to_string(index) +
                                     " is out of range for a " +
                                     std::to_string(width) + "-wide signal");
        }
        s.outputWidths[0] = static_cast<int>(indices_.size());
    }

    void computeOutputs(const EvalContext& c) override {
        const Vec& u = c.in(0);
        Vec& y = c.out(0);
        for (std::size_t i = 0; i < indices_.size(); ++i)
            y[static_cast<Eigen::Index>(i)] =
                indices_[i] < u.size() ? u[indices_[i]] : 0.0;
    }

private:
    std::vector<int> indices_;
};

class SwitchBlock : public Block {
public:
    PortLayout ports() const override { return {{"1", "ctrl", "3"}, {"out"}}; }

    void setup(BlockSetup& s) override {
        threshold_ = params().real("threshold", 0.0);
        criterion_ = params().text("criterion", ">=");

        int width = 1;
        for (int i : {0, 2})
            if (s.inputConnected[i]) width = std::max(width, s.inputWidths[i]);
        width_ = width;
        s.outputWidths[0] = width;
        s.zeroCrossings = s.inputConnected[1] ? std::max(1, s.inputWidths[1])
                                              : 1;
    }

    void computeZeroCrossings(const EvalContext& c,
                              Eigen::Ref<Vec> zc) override {
        const Vec& control = c.in(1);
        for (Eigen::Index i = 0; i < zc.size(); ++i)
            zc[i] = (control.size() == 1 ? control[0]
                                         : (i < control.size() ? control[i] : 0.0)) -
                    threshold_;
    }

    void computeOutputs(const EvalContext& c) override {
        const Vec& control = c.in(1);
        Vec& y = c.out(0);

        for (Eigen::Index i = 0; i < y.size(); ++i) {
            const double gate =
                control.size() == 1 ? control[0]
                                    : (i < control.size() ? control[i] : 0.0);
            bool takeFirst;
            if (criterion_ == ">") takeFirst = gate > threshold_;
            else if (criterion_ == "!=") takeFirst = gate != threshold_;
            else takeFirst = gate >= threshold_;

            const Vec& source = takeFirst ? c.in(0) : c.in(2);
            y[i] = source.size() == 1 ? source[0]
                                      : (i < source.size() ? source[i] : 0.0);
        }
    }

private:
    double threshold_ = 0.0;
    int width_ = 1;
    std::string criterion_ = ">=";
};

class ManualSwitchBlock : public Block {
public:
    PortLayout ports() const override { return {{"1", "2"}, {"out"}}; }

    void setup(BlockSetup& s) override {
        second_ = params().boolean("selectSecond", false);
        s.outputWidths[0] = s.inheritedWidth();
    }

    void computeOutputs(const EvalContext& c) override {
        const Vec& source = c.in(second_ ? 1 : 0);
        Vec& y = c.out(0);
        for (Eigen::Index i = 0; i < y.size(); ++i)
            y[i] = source.size() == 1 ? source[0]
                                      : (i < source.size() ? source[i] : 0.0);
    }

private:
    bool second_ = false;
};

class GotoBlock : public Block {
public:
    PortLayout ports() const override { return {{"in"}, {}}; }

    void setup(BlockSetup& s) override {
        (void)s;
        throw ModelError("a Goto block should have been spliced away before "
                         "compilation");
    }

    void computeOutputs(const EvalContext& c) override { (void)c; }
};

class FromBlock : public Block {
public:
    PortLayout ports() const override { return {{}, {"out"}}; }

    void setup(BlockSetup& s) override {
        (void)s;
        throw ModelError("a From block should have been spliced away before "
                         "compilation");
    }

    void computeOutputs(const EvalContext& c) override { (void)c; }
};

}

bool isGoto(const Block* block) {
    return block && block->typeName() == "Goto";
}

bool isFrom(const Block* block) {
    return block && block->typeName() == "From";
}

std::string signalTagOf(const Block* block) {
    if (!block) return {};
    const std::string tag = block->params().text("tag", "");
    // Tags match by name, so blanks would make two tags that look identical.
    const auto first = tag.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = tag.find_last_not_of(" \t\r\n");
    return tag.substr(first, last - first + 1);
}

bool isGlobalGoto(const Block* block) {
    return block && block->params().text("scope", "local") == "global";
}

void registerRoutingBlocks() {
    registerBlockType<MuxBlock>(
        "Mux", "Routing", "Bundles several signals into one vector signal.",
        {intParam("inputs", "Number of inputs", 2, 1, 64)}, 24.0, 80.0);

    registerBlockType<DemuxBlock>(
        "Demux", "Routing", "Splits a vector signal into several signals.",
        {intParam("outputs", "Number of outputs", 2, 1, 64),
         vectorParam("widths", "Output widths", {},
                     "Leave empty to split evenly.")},
        24.0, 80.0);

    registerBlockType<SelectorBlock>(
        "Selector", "Routing", "Extracts elements of a vector by index.",
        {vectorParam("indices", "Indices (zero-based)", {0.0})}, 80.0, 55.0);

    registerBlockType<SwitchBlock>(
        "Switch", "Routing",
        "Routes input 1 or input 3 depending on the control input.",
        {choiceParam("criterion", "Pass input 1 when control", {">=", ">", "!="},
                     std::string(">=")),
         realParam("threshold", "Threshold", 0.0)},
        70.0, 70.0);

    registerBlockType<ManualSwitchBlock>(
        "ManualSwitch", "Routing", "Selects one of two inputs by hand.",
        {boolParam("selectSecond", "Select second input", false)}, 70.0, 60.0);

    registerBlockType<GotoBlock>(
        "Goto", "Routing",
        "Sends its input to every From block with the same tag, without a "
        "wire. Costs nothing at run time: the link is spliced away when the "
        "model is flattened.",
        {textParam("tag", "Tag", "A",
                   "The name this signal answers to. Matched exactly."),
         choiceParam("scope", "Visible", {"local", "global"},
                     std::string("local"),
                     "'local' is reachable from the same diagram only, which "
                     "is what keeps two copies of one subsystem from stealing "
                     "each other's signals. 'global' is reachable from "
                     "anywhere in the model.")},
        80.0, 45.0);

    registerBlockType<FromBlock>(
        "From", "Routing",
        "Reproduces the signal fed to the Goto block with the same tag.",
        {textParam("tag", "Tag", "A",
                   "Matched against a Goto in the same diagram first, then "
                   "against any global Goto.")},
        80.0, 45.0);
}

}
