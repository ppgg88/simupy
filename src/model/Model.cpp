#include "Model.h"

#include "BlockRegistry.h"

#include <algorithm>

namespace simupy {

const char* SolverSettings::methodName(Method m) {
    switch (m) {
        case Method::Euler: return "euler";
        case Method::Heun: return "heun";
        case Method::RK4: return "rk4";
        case Method::RK45: return "rk45";
        case Method::SDIRK2: return "sdirk2";
        case Method::DiscreteOnly: return "discrete";
    }
    return "rk45";
}

SolverSettings::Method SolverSettings::methodFromName(const std::string& name) {
    if (name == "euler") return Method::Euler;
    if (name == "heun") return Method::Heun;
    if (name == "rk4") return Method::RK4;
    if (name == "sdirk2") return Method::SDIRK2;
    if (name == "discrete") return Method::DiscreteOnly;
    return Method::RK45;
}

std::string Model::allocateId(const std::string& prefix) {
    std::string candidate;
    do {
        candidate = prefix + "_" + std::to_string(++idCounter_);
    } while (block(candidate) != nullptr);
    return candidate;
}

Block* Model::addBlock(BlockPtr block, BlockGeometry geometry) {
    if (!block) throw ModelError("cannot add a null block");

    if (block->id().empty() || this->block(block->id()))
        block->setId(allocateId(block->typeName().empty() ? "block"
                                                          : block->typeName()));
    if (block->name().empty()) block->setName(block->typeName());
    block->setName(uniqueName(block->name(), block->id()));

    geometry_[block->id()] = geometry;
    blocks_.push_back(std::move(block));
    return blocks_.back().get();
}

Block* Model::addBlock(const std::string& typeName, double x, double y) {
    const BlockType* type = BlockRegistry::instance().find(typeName);
    BlockGeometry geometry;
    geometry.x = x;
    geometry.y = y;
    if (type) {
        geometry.width = type->defaultWidth;
        geometry.height = type->defaultHeight;
    }
    return addBlock(BlockRegistry::instance().create(typeName), geometry);
}

void Model::removeBlock(const std::string& blockId) {
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
                       [&](const Connection& c) {
                           return c.sourceBlock == blockId ||
                                  c.targetBlock == blockId;
                       }),
        connections_.end());
    geometry_.erase(blockId);
    blocks_.erase(std::remove_if(blocks_.begin(), blocks_.end(),
                                 [&](const BlockPtr& b) {
                                     return b->id() == blockId;
                                 }),
                  blocks_.end());
}

Block* Model::block(const std::string& blockId) {
    for (const BlockPtr& b : blocks_)
        if (b->id() == blockId) return b.get();
    return nullptr;
}

const Block* Model::block(const std::string& blockId) const {
    return const_cast<Model*>(this)->block(blockId);
}

BlockGeometry& Model::geometry(const std::string& blockId) {
    auto it = geometry_.find(blockId);
    if (it == geometry_.end())
        throw ModelError("no geometry for block '" + blockId + "'");
    return it->second;
}

const BlockGeometry& Model::geometry(const std::string& blockId) const {
    return const_cast<Model*>(this)->geometry(blockId);
}

bool Model::isNameAvailable(const std::string& name,
                            const std::string& exceptId) const {
    for (const BlockPtr& b : blocks_)
        if (b->name() == name && b->id() != exceptId) return false;
    return true;
}

std::string Model::uniqueName(const std::string& base,
                              const std::string& exceptId) const {
    if (isNameAvailable(base, exceptId)) return base;
    for (int i = 1;; ++i) {
        std::string candidate = base + std::to_string(i);
        if (isNameAvailable(candidate, exceptId)) return candidate;
    }
}

const Connection& Model::connect(const std::string& sourceBlock, int sourcePort,
                                 const std::string& targetBlock,
                                 int targetPort) {
    const Block* source = block(sourceBlock);
    const Block* target = block(targetBlock);
    if (!source) throw ModelError("unknown source block '" + sourceBlock + "'");
    if (!target) throw ModelError("unknown target block '" + targetBlock + "'");

    const PortLayout sourcePorts = source->ports();
    const PortLayout targetPorts = target->ports();
    if (sourcePort < 0 ||
        sourcePort >= static_cast<int>(sourcePorts.outputs.size()))
        throw ModelError(source->name() + " has no output port " +
                         std::to_string(sourcePort));
    if (targetPort < 0 ||
        targetPort >= static_cast<int>(targetPorts.inputs.size()))
        throw ModelError(target->name() + " has no input port " +
                         std::to_string(targetPort));

    disconnectInput(targetBlock, targetPort);

    Connection connection;
    connection.id = allocateId("wire");
    connection.sourceBlock = sourceBlock;
    connection.sourcePort = sourcePort;
    connection.targetBlock = targetBlock;
    connection.targetPort = targetPort;
    connections_.push_back(std::move(connection));
    return connections_.back();
}

void Model::disconnect(const std::string& connectionId) {
    connections_.erase(std::remove_if(connections_.begin(), connections_.end(),
                                      [&](const Connection& c) {
                                          return c.id == connectionId;
                                      }),
                       connections_.end());
}

void Model::disconnectInput(const std::string& targetBlock, int targetPort) {
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
                       [&](const Connection& c) {
                           return c.targetBlock == targetBlock &&
                                  c.targetPort == targetPort;
                       }),
        connections_.end());
}

const Connection* Model::incoming(const std::string& blockId, int port) const {
    for (const Connection& c : connections_)
        if (c.targetBlock == blockId && c.targetPort == port) return &c;
    return nullptr;
}

void Model::pruneDanglingConnections() {
    connections_.erase(
        std::remove_if(
            connections_.begin(), connections_.end(),
            [&](const Connection& c) {
                const Block* source = block(c.sourceBlock);
                const Block* target = block(c.targetBlock);
                if (!source || !target) return true;
                const int outputs =
                    static_cast<int>(source->ports().outputs.size());
                const int inputs =
                    static_cast<int>(target->ports().inputs.size());
                return c.sourcePort >= outputs || c.targetPort >= inputs;
            }),
        connections_.end());
}

void Model::clear() {
    connections_.clear();
    geometry_.clear();
    blocks_.clear();
    idCounter_ = 0;
    initScript_.clear();
    solver_ = SolverSettings{};
}

}  // namespace simupy
