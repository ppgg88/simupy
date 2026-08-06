#pragma once

#include "Block.h"

#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace simupy {

struct Connection {
    std::string id;
    std::string sourceBlock;
    int sourcePort = 0;
    std::string targetBlock;
    int targetPort = 0;
    std::vector<std::pair<double, double>> waypoints;
};

struct BlockGeometry {
    double x = 0.0;
    double y = 0.0;
    double width = 80.0;
    double height = 60.0;
    int rotation = 0;
    bool mirrored = false;
};

struct SolverSettings {
    enum class Method {
        Euler,
        Heun,
        RK4,
        RK45,
        SDIRK2,
        DiscreteOnly
    };

    Method method = Method::RK45;
    double startTime = 0.0;
    double stopTime = 10.0;
    double fixedStep = 0.001;
    double maxStep = 0.0;
    double minStep = 1e-12;
    double initialStep = 0.0;
    double relTol = 1e-4;
    double absTol = 1e-7;

    bool detectZeroCrossings = true;

    int maxLoopIterations = 100;
    double loopTolerance = 1e-9;

    int maxLoggedSamples = 200000;

    bool realTime = false;

    double realTimeFactor = 1.0;

    bool unbounded = false;

    double effectiveStopTime() const {
        return unbounded ? std::numeric_limits<double>::infinity() : stopTime;
    }

    double effectiveMaxStep() const {
        if (maxStep > 0.0) return maxStep;
        if (unbounded) return kUnboundedMaxStep;
        return (stopTime - startTime) / 200.0;
    }

    static constexpr double kUnboundedMaxStep = 0.05;

    static const char* methodName(Method m);
    static Method methodFromName(const std::string& name);
};

class Model {
public:
    Model() = default;
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    Block* addBlock(BlockPtr block, BlockGeometry geometry = {});

    Block* addBlock(const std::string& typeName, double x, double y);

    void removeBlock(const std::string& blockId);

    BlockPtr takeBlock(const std::string& blockId);

    Block* block(const std::string& blockId);
    const Block* block(const std::string& blockId) const;

    const std::vector<BlockPtr>& blocks() const { return blocks_; }

    BlockGeometry& geometry(const std::string& blockId);
    const BlockGeometry& geometry(const std::string& blockId) const;

    bool isNameAvailable(const std::string& name,
                         const std::string& exceptId = {}) const;
    std::string uniqueName(const std::string& base,
                           const std::string& exceptId = {}) const;

    const Connection& connect(const std::string& sourceBlock, int sourcePort,
                              const std::string& targetBlock, int targetPort);

    void disconnect(const std::string& connectionId);

    void disconnectInput(const std::string& targetBlock, int targetPort);

    const std::vector<Connection>& connections() const { return connections_; }

    const Connection* incoming(const std::string& blockId, int port) const;

    void pruneDanglingConnections();

    SolverSettings& solver() { return solver_; }
    const SolverSettings& solver() const { return solver_; }

    const std::string& name() const { return name_; }
    void setName(std::string v) { name_ = std::move(v); }

    /// Python packages the model's own blocks import. A block written in the
    /// diagram belongs to no library, so the model is the only place to say.
    std::vector<PackageRequirement>& pythonPackages() { return python_; }
    const std::vector<PackageRequirement>& pythonPackages() const {
        return python_;
    }

    const std::string& initScript() const { return initScript_; }
    void setInitScript(std::string v) { initScript_ = std::move(v); }

    void clear();

private:
    std::string allocateId(const std::string& prefix);

    std::vector<BlockPtr> blocks_;
    std::map<std::string, BlockGeometry> geometry_;
    std::vector<Connection> connections_;
    SolverSettings solver_;
    std::string name_ = "untitled";
    std::string initScript_;
    std::vector<PackageRequirement> python_;
    int idCounter_ = 0;
};

}
