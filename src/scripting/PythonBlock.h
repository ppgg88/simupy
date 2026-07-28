#pragma once

#include "model/Block.h"

#include <memory>
#include <string>

namespace simupy {

class PythonBlock : public Block {
public:
    PythonBlock();
    ~PythonBlock() override;

    PortLayout ports() const override;
    void setup(BlockSetup& s) override;

    void initialize(Eigen::Ref<Vec> xc, Eigen::Ref<Vec> xd) override;
    void computeOutputs(const EvalContext& c) override;
    void computeDerivatives(const EvalContext& c, Eigen::Ref<Vec> dxc) override;
    void updateDiscrete(const EvalContext& c, Eigen::Ref<Vec> xdNext) override;
    void computeZeroCrossings(const EvalContext& c, Eigen::Ref<Vec> zc) override;
    void terminate() override;
    void reset() override;

    bool logsInputs() const override { return logsInputs_; }

    /// Compiles the current source and returns an empty string if the block
    /// looks usable, or a diagnostic otherwise. Used by the editor.
    std::string validate() const;

    /// Source of the last compilation error, for the canvas to show a badge.
    const std::string& lastError() const { return lastError_; }

    static const char* defaultSource();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    mutable std::string lastError_;
    bool logsInputs_ = false;
};

void registerPythonBlocks();

}  // namespace simupy
