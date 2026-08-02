#include "SinkBlocks.h"

#include "BlockUtils.h"

#include <fstream>
#include <iomanip>

namespace simupy {
namespace {

class ScopeBlock : public Block {
public:
    PortLayout ports() const override {
        const int count = std::max(1, params().integer("inputs", 1));
        if (count == 1) return {{"in"}, {}};
        return {numberedPorts("in", count), {}};
    }

    void setup(BlockSetup& s) override {
        s.outputWidths.clear();
        s.directFeedthrough.assign(s.inputCount(), true);
    }

    void computeOutputs(const EvalContext& c) override { (void)c; }

    bool logsInputs() const override { return true; }
};

class DisplayBlock : public DisplaySink {
public:
    PortLayout ports() const override { return {{"in"}, {}}; }

    void setup(BlockSetup& s) override {
        s.outputWidths.clear();
        s.directFeedthrough.assign(s.inputCount(), true);
    }

    void initialize(Eigen::Ref<Vec> xc, Eigen::Ref<Vec> xd) override {
        (void)xc;
        (void)xd;
        clearValue();
    }

    void computeOutputs(const EvalContext& c) override {
        if (c.majorStep) publish(c.in(0));
    }

    bool logsInputs() const override { return true; }
};

class TerminatorBlock : public Block {
public:
    PortLayout ports() const override { return {{"in"}, {}}; }

    void setup(BlockSetup& s) override {
        s.outputWidths.clear();
        s.directFeedthrough.assign(s.inputCount(), false);
    }

    void computeOutputs(const EvalContext& c) override { (void)c; }
};

class ToFileBlock : public Block {
public:
    PortLayout ports() const override { return {{"in"}, {}}; }

    void setup(BlockSetup& s) override {
        path_ = params().text("path", "simupy_output.csv");
        if (path_.empty()) throw ModelError("the output path is empty");
        precision_ = params().integer("precision", 9);
        width_ = s.inheritedWidth();

        s.outputWidths.clear();
        s.directFeedthrough.assign(s.inputCount(), true);

        openStream();
    }

    void computeOutputs(const EvalContext& c) override {
        if (!c.majorStep || !stream_.is_open()) return;

        stream_ << c.t;
        const Vec& u = c.in(0);
        for (Eigen::Index i = 0; i < u.size(); ++i) stream_ << ',' << u[i];
        stream_ << '\n';
    }

    void terminate() override {
        if (stream_.is_open()) stream_.close();
    }

    void reset() override { terminate(); }

private:
    void openStream() {
        stream_.close();
        stream_.clear();
        stream_.open(path_, std::ios::out | std::ios::trunc);
        if (!stream_)
            throw ModelError("cannot open '" + path_ + "' for writing");
        stream_ << std::setprecision(precision_);

        stream_ << "time";
        for (int i = 0; i < width_; ++i) stream_ << ",signal" << i;
        stream_ << '\n';
    }

    std::string path_ = "simupy_output.csv";
    int precision_ = 9;
    int width_ = 1;
    std::ofstream stream_;
};

}

bool isDisplay(const Block* block) {
    return dynamic_cast<const DisplaySink*>(block) != nullptr;
}

Vec displayedValue(const Block* block) {
    const auto* sink = dynamic_cast<const DisplaySink*>(block);
    return sink ? sink->latest() : Vec();
}

void registerSinkBlocks() {
    registerBlockType<ScopeBlock>(
        "Scope", "Sinks", "Records and plots its inputs against time.",
        {intParam("inputs", "Number of inputs", 1, 1, 32),
         boolParam("autoscale", "Auto-scale the vertical axis", true),
         realParam("yMin", "Y minimum", -1.0),
         realParam("yMax", "Y maximum", 1.0),
         realParam("window", "Time window (s)", 0.0,
                   "Seconds kept on screen while the model runs. 0 shows the "
                   "whole run.")},
        70.0, 60.0);

    registerBlockType<DisplayBlock>("Display", "Sinks",
                                    "Shows the current value of a signal.", {},
                                    80.0, 45.0);

    registerBlockType<TerminatorBlock>(
        "Terminator", "Sinks", "Absorbs an unused signal.", {}, 50.0, 40.0);

    registerBlockType<ToFileBlock>(
        "ToFile", "Sinks", "Writes the signal to a CSV file during the run.",
        {textParam("path", "File path", "simupy_output.csv"),
         intParam("precision", "Decimal digits", 9, 1, 17)},
        90.0, 55.0);
}

}
