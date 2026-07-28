#pragma once

#include "Types.h"

#include <cctype>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace simupy {

struct PortLayout {
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
};

struct BlockSetup {
    std::vector<int> inputWidths;      ///< resolved width of each input signal
    std::vector<bool> inputConnected;  ///< false for dangling inputs

    std::vector<int> outputWidths;
    int continuousStates = 0;
    int discreteStates = 0;
    int zeroCrossings = 0;
    double sampleTime = kContinuousSampleTime;
    std::vector<bool> directFeedthrough;

    int inputCount() const { return static_cast<int>(inputWidths.size()); }
    int outputCount() const { return static_cast<int>(outputWidths.size()); }

    int inheritedWidth() const {
        int w = 1;
        for (std::size_t i = 0; i < inputWidths.size(); ++i)
            if (inputConnected[i]) w = std::max(w, inputWidths[i]);
        return w;
    }
};

struct EvalContext {
    double t = 0.0;

    const Vec* const* u = nullptr;  ///< nu pointers to input signal values
    int nu = 0;
    Vec* y = nullptr;  ///< ny output signal buffers to write into
    int ny = 0;

    const double* xc = nullptr;  ///< this block's continuous state slice
    int nxc = 0;
    const double* xd = nullptr;  ///< this block's discrete state slice
    int nxd = 0;

    /// True on an accepted step. Solvers evaluate intermediate stages with
    /// majorStep == false; sinks must not log those.
    bool majorStep = true;

    const Vec& in(int i) const { return *u[i]; }
    Vec& out(int i) const { return y[i]; }

    Eigen::Map<const Vec> x() const {
        return Eigen::Map<const Vec>(xc, nxc);
    }
    Eigen::Map<const Vec> xdisc() const {
        return Eigen::Map<const Vec>(xd, nxd);
    }
};

class Block {
public:
    virtual ~Block() = default;

    const std::string& id() const { return id_; }
    void setId(std::string v) { id_ = std::move(v); }

    const std::string& typeName() const { return typeName_; }
    void setTypeName(std::string v) { typeName_ = std::move(v); }

    const std::string& name() const { return name_; }
    void setName(std::string v) { name_ = std::move(v); }

    Parameters& params() { return params_; }
    const Parameters& params() const { return params_; }

    const std::map<std::string, std::string>& paramExpressions() const {
        return expressions_;
    }

    void setParamExpression(const std::string& name, std::string expression) {
        if (expression.find_first_not_of(" \t\r\n") == std::string::npos)
            expressions_.erase(name);
        else
            expressions_[name] = std::move(expression);
    }

    std::string paramExpression(const std::string& name) const {
        auto it = expressions_.find(name);
        return it == expressions_.end() ? std::string() : it->second;
    }

    /// User-chosen labels for this block's output signals, keyed by port.
    ///
    /// The name belongs to the port rather than to the wire because every
    /// wire leaving one output carries the same signal: naming a branch would
    /// otherwise let one signal answer to two names. Empty for most blocks —
    /// an unnamed signal is labelled after the block that produces it.
    const std::map<int, std::string>& signalNames() const { return signals_; }

    void setSignalName(int outputPort, std::string name) {
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
            name.pop_back();
        if (name.empty()) signals_.erase(outputPort);
        else signals_[outputPort] = std::move(name);
    }

    std::string signalName(int outputPort) const {
        auto it = signals_.find(outputPort);
        return it == signals_.end() ? std::string() : it->second;
    }

    /// Port names, computed from the current parameters. Called whenever a
    /// parameter changes so the canvas can redraw the block.
    virtual PortLayout ports() const = 0;

    virtual void setup(BlockSetup& s) = 0;

    virtual void initialize(Eigen::Ref<Vec> xc, Eigen::Ref<Vec> xd) {
        xc.setZero();
        xd.setZero();
    }

    /// y = f(t, u, x). Must not have side effects other than writing outputs,
    /// because solvers call it several times per accepted step.
    virtual void computeOutputs(const EvalContext& c) = 0;

    virtual void computeDerivatives(const EvalContext& c, Eigen::Ref<Vec> dxc) {
        (void)c;
        dxc.setZero();
    }

    virtual void updateDiscrete(const EvalContext& c, Eigen::Ref<Vec> xdNext) {
        xdNext = c.xdisc();
    }

    /// Signed quantities whose sign change marks a discontinuity.
    ///
    /// A Saturation, for instance, reports `u - upper` and `u - lower`: the
    /// output is smooth as long as neither changes sign. The simulator
    /// brackets any change and lands the step on the event, so the solver
    /// never has to integrate across the kink.
    ///
    /// Called right after computeOutputs(), so inputs and outputs are current.
    virtual void computeZeroCrossings(const EvalContext& c,
                                      Eigen::Ref<Vec> zc) {
        (void)c;
        zc.setZero();
    }

    virtual bool logsInputs() const { return false; }

    virtual void terminate() {}

    virtual void reset() {}

protected:
    std::string id_;
    std::string typeName_;
    std::string name_;
    Parameters params_;
    std::map<std::string, std::string> expressions_;
    std::map<int, std::string> signals_;
};

using BlockPtr = std::unique_ptr<Block>;

}  // namespace simupy
