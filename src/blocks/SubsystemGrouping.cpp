#include "SubsystemGrouping.h"

#include "SubsystemBlock.h"

#include "model/BlockRegistry.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <variant>

namespace simupy {
namespace {

constexpr double kPortColumn = 140.0;
constexpr double kMargin = 60.0;

double snapped(double value) { return std::round(value / 10.0) * 10.0; }

struct Endpoint {
    std::string block;
    int port = 0;

    bool operator<(const Endpoint& other) const {
        if (block != other.block) return block < other.block;
        return port < other.port;
    }
};

struct Crossing {
    Endpoint source;
    std::vector<Connection> wires;
    double x = 0.0;
    double y = 0.0;
};


std::string kindOf(const ParamValue& value) {
    if (std::holds_alternative<bool>(value)) return "a true/false";
    if (std::holds_alternative<std::string>(value)) return "text";
    if (std::holds_alternative<std::vector<double>>(value)) return "a list";
    return "a number";
}

/// Whether a resolved value is the kind the block's own parameter takes.
bool fits(const Block& block, const std::string& name, const ParamValue& value) {
    const BlockType* type = BlockRegistry::instance().find(block.typeName());
    if (!type) return true;

    for (const ParamSpec& spec : type->params) {
        if (spec.name != name) continue;
        const bool wantsText = spec.kind == ParamSpec::Kind::Text ||
                               spec.kind == ParamSpec::Kind::Choice ||
                               spec.kind == ParamSpec::Kind::PythonCode;
        return wantsText == std::holds_alternative<std::string>(value);
    }
    return true;
}

std::vector<Crossing> inDrawingOrder(const std::map<Endpoint, Crossing>& found) {
    std::vector<Crossing> ordered;
    ordered.reserve(found.size());
    for (const auto& [endpoint, crossing] : found) ordered.push_back(crossing);

    // Stable sort: the map's block-and-port order breaks ties repeatably.
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const Crossing& a, const Crossing& b) {
                         if (a.y != b.y) return a.y < b.y;
                         return a.x < b.x;
                     });
    return ordered;
}

}

GroupResult groupIntoSubsystem(Model& model,
                               const std::vector<std::string>& blockIds) {
    const std::set<std::string> inside(blockIds.begin(), blockIds.end());
    if (inside.empty()) throw ModelError("no blocks were selected to group");

    // Checked before the first block moves, so a refusal changes nothing.
    for (const std::string& id : inside) {
        const Block* block = model.block(id);
        if (!block)
            throw ModelError("no block '" + id + "' in this diagram");
        if (isInport(block) || isOutport(block))
            throw ModelError("'" + block->name() +
                             "' defines a port of the diagram it sits in, so "
                             "it cannot be moved into a subsystem");
    }

    // Recorded first: taking a block out drops the wires touching it.
    std::vector<Connection> internal;
    std::map<Endpoint, Crossing> entering;
    std::map<Endpoint, Crossing> leaving;

    for (const Connection& wire : model.connections()) {
        const bool fromInside = inside.count(wire.sourceBlock) != 0;
        const bool toInside = inside.count(wire.targetBlock) != 0;

        if (fromInside && toInside) {
            internal.push_back(wire);
        } else if (toInside) {
            Crossing& crossing = entering[{wire.sourceBlock, wire.sourcePort}];
            crossing.source = {wire.sourceBlock, wire.sourcePort};
            crossing.wires.push_back(wire);
        } else if (fromInside) {
            Crossing& crossing = leaving[{wire.sourceBlock, wire.sourcePort}];
            crossing.source = {wire.sourceBlock, wire.sourcePort};
            crossing.wires.push_back(wire);
        }
    }

    for (auto& [endpoint, crossing] : entering) {
        const BlockGeometry& target =
            model.geometry(crossing.wires.front().targetBlock);
        crossing.x = target.x;
        crossing.y = target.y;
    }
    for (auto& [endpoint, crossing] : leaving) {
        const BlockGeometry& source = model.geometry(crossing.source.block);
        crossing.x = source.x;
        crossing.y = source.y;
    }

    const std::vector<Crossing> inputs = inDrawingOrder(entering);
    const std::vector<Crossing> outputs = inDrawingOrder(leaving);

    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
    bool firstBlock = true;
    for (const std::string& id : inside) {
        const BlockGeometry& geometry = model.geometry(id);
        if (firstBlock) {
            minX = geometry.x;
            minY = geometry.y;
            maxX = geometry.x + geometry.width;
            maxY = geometry.y + geometry.height;
            firstBlock = false;
            continue;
        }
        minX = std::min(minX, geometry.x);
        minY = std::min(minY, geometry.y);
        maxX = std::max(maxX, geometry.x + geometry.width);
        maxY = std::max(maxY, geometry.y + geometry.height);
    }

    Block* subsystem = model.addBlock("Subsystem", 0.0, 0.0);
    BlockGeometry& placed = model.geometry(subsystem->id());
    placed.x = snapped((minX + maxX) / 2.0 - placed.width / 2.0);
    placed.y = snapped((minY + maxY) / 2.0 - placed.height / 2.0);

    Model& contents = dynamic_cast<SubsystemBlock&>(*subsystem).contents();

    const double dx = kMargin + kPortColumn - minX;
    const double dy = kMargin - minY;

    std::vector<std::string> moving;
    for (const BlockPtr& block : model.blocks())
        if (inside.count(block->id()) != 0) moving.push_back(block->id());

    for (const std::string& id : moving) {
        BlockGeometry geometry = model.geometry(id);
        BlockPtr block = model.takeBlock(id);
        geometry.x += dx;
        geometry.y += dy;
        contents.addBlock(std::move(block), geometry);
    }

    // Waypoints are dropped; the wires reroute in their new diagram.
    for (const Connection& wire : internal)
        contents.connect(wire.sourceBlock, wire.sourcePort, wire.targetBlock,
                         wire.targetPort);

    double innerRight = kMargin + kPortColumn;
    for (const BlockPtr& block : contents.blocks()) {
        const BlockGeometry& geometry = contents.geometry(block->id());
        innerRight = std::max(innerRight, geometry.x + geometry.width);
    }

    int index = 0;
    for (const Crossing& crossing : inputs) {
        ++index;
        Block* inport = contents.addBlock("Inport", snapped(kMargin),
                                          snapped(crossing.y + dy));
        inport->params().set("portNumber", static_cast<double>(index));

        const Block* source = model.block(crossing.source.block);
        std::string label =
            source ? source->signalName(crossing.source.port) : std::string();
        if (label.empty()) label = "in" + std::to_string(index);
        inport->setName(contents.uniqueName(label, inport->id()));

        for (const Connection& wire : crossing.wires)
            contents.connect(inport->id(), 0, wire.targetBlock, wire.targetPort);

        model.connect(crossing.source.block, crossing.source.port,
                      subsystem->id(), index - 1);
    }

    index = 0;
    for (const Crossing& crossing : outputs) {
        ++index;
        Block* outport =
            contents.addBlock("Outport", snapped(innerRight + kMargin),
                              snapped(crossing.y + dy));
        outport->params().set("portNumber", static_cast<double>(index));

        const Block* source = contents.block(crossing.source.block);
        std::string label =
            source ? source->signalName(crossing.source.port) : std::string();
        if (label.empty()) label = "out" + std::to_string(index);
        outport->setName(contents.uniqueName(label, outport->id()));

        contents.connect(crossing.source.block, crossing.source.port,
                         outport->id(), 0);

        for (const Connection& wire : crossing.wires)
            model.connect(subsystem->id(), index - 1, wire.targetBlock,
                          wire.targetPort);
    }

    GroupResult result;
    result.subsystemId = subsystem->id();
    result.inputs = static_cast<int>(inputs.size());
    result.outputs = static_cast<int>(outputs.size());
    return result;
}

UngroupResult ungroupSubsystem(Model& model, const std::string& subsystemId,
                               const ExpressionEvaluator& evaluate) {
    Block* block = model.block(subsystemId);
    if (!block) throw ModelError("no block '" + subsystemId + "' in this diagram");

    auto* subsystem = dynamic_cast<SubsystemBlock*>(block);
    if (!subsystem)
        throw ModelError("'" + block->name() +
                         "' is not a subsystem, so there is nothing to break "
                         "open");

    Model& contents = subsystem->contents();
    UngroupResult result;

    // Resolved while the mask still exists: its scope goes with the block.
    const std::map<std::string, ParamValue> mask = block->params().all();
    std::vector<std::tuple<Block*, std::string, ParamValue>> resolved;
    for (const BlockPtr& inner : contents.blocks()) {
        if (inner->paramExpressions().empty()) continue;
        if (!evaluate)
            throw ModelError(
                "'" + inner->name() + "' inside '" + block->name() +
                "' takes a parameter from the subsystem's mask, and there is "
                "no evaluator here to work out what it comes to");

        for (const auto& [name, expression] : inner->paramExpressions()) {
            ParamValue value =
                evaluate(expression, mask,
                         "parameter '" + name + "' of " + inner->name());
            if (!fits(*inner, name, value))
                throw ModelError("'" + expression + "' gives " +
                                 kindOf(value) + ", which is not what the '" +
                                 name + "' parameter of " + inner->name() +
                                 " holds");
            resolved.emplace_back(inner.get(), name, std::move(value));
        }
    }

    // Written only once every expression has been worked out, so a refusal
    // leaves the diagram as it was.
    for (auto& [inner, name, value] : resolved) {
        inner->params().set(name, std::move(value));
        inner->setParamExpression(name, {});
        ++result.resolvedParameters;
    }

    // Recorded before anything is taken apart.
    struct Endpoint {
        std::string block;
        int port = 0;
    };
    std::map<int, Endpoint> feeding;
    std::map<int, std::vector<Endpoint>> fed;
    for (const Connection& wire : model.connections()) {
        if (wire.targetBlock == subsystemId)
            feeding[wire.targetPort] = {wire.sourceBlock, wire.sourcePort};
        else if (wire.sourceBlock == subsystemId)
            fed[wire.sourcePort].push_back({wire.targetBlock, wire.targetPort});
    }

    const std::vector<Block*> inports = subsystem->portBlocks(true);
    const std::vector<Block*> outports = subsystem->portBlocks(false);

    std::set<std::string> portBlocks;
    std::map<std::string, int> inportIndex;
    for (std::size_t i = 0; i < inports.size(); ++i) {
        if (!inports[i]) continue;
        portBlocks.insert(inports[i]->id());
        inportIndex[inports[i]->id()] = static_cast<int>(i);
    }
    for (Block* outport : outports)
        if (outport) portBlocks.insert(outport->id());

    const std::vector<Connection> innerWires = contents.connections();

    std::vector<std::string> moving;
    for (const BlockPtr& inner : contents.blocks())
        if (portBlocks.count(inner->id()) == 0) moving.push_back(inner->id());

    double dx = 0.0, dy = 0.0;
    if (!moving.empty()) {
        double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
        bool firstBlock = true;
        for (const std::string& id : moving) {
            const BlockGeometry& geometry = contents.geometry(id);
            if (firstBlock) {
                minX = geometry.x;
                minY = geometry.y;
                maxX = geometry.x + geometry.width;
                maxY = geometry.y + geometry.height;
                firstBlock = false;
                continue;
            }
            minX = std::min(minX, geometry.x);
            minY = std::min(minY, geometry.y);
            maxX = std::max(maxX, geometry.x + geometry.width);
            maxY = std::max(maxY, geometry.y + geometry.height);
        }

        const BlockGeometry& placed = model.geometry(subsystemId);
        dx = (placed.x + placed.width / 2.0) - (minX + maxX) / 2.0;
        dy = (placed.y + placed.height / 2.0) - (minY + maxY) / 2.0;
    }

    // Ids and names are per diagram, so addBlock may hand out fresh ones.
    std::map<std::string, std::string> arrived;
    for (const std::string& id : moving) {
        BlockGeometry geometry = contents.geometry(id);
        BlockPtr inner = contents.takeBlock(id);
        geometry.x = snapped(geometry.x + dx);
        geometry.y = snapped(geometry.y + dy);

        Block* landed = model.addBlock(std::move(inner), geometry);
        arrived[id] = landed->id();
        result.blockIds.push_back(landed->id());
    }

    auto movedId = [&arrived](const std::string& id) {
        const auto found = arrived.find(id);
        return found == arrived.end() ? std::string() : found->second;
    };

    for (const Connection& wire : innerWires) {
        if (portBlocks.count(wire.sourceBlock) != 0) continue;
        if (portBlocks.count(wire.targetBlock) != 0) continue;
        model.connect(movedId(wire.sourceBlock), wire.sourcePort,
                      movedId(wire.targetBlock), wire.targetPort);
    }

    for (std::size_t index = 0; index < inports.size(); ++index) {
        if (!inports[index]) continue;
        const auto source = feeding.find(static_cast<int>(index));
        if (source == feeding.end()) continue;

        for (const Connection& wire : innerWires) {
            if (wire.sourceBlock != inports[index]->id()) continue;
            const std::string target = movedId(wire.targetBlock);
            // An Inport wired to an Outport is joined from the output side below.
            if (target.empty()) continue;
            model.connect(source->second.block, source->second.port, target,
                          wire.targetPort);
        }
    }

    for (std::size_t index = 0; index < outports.size(); ++index) {
        Block* outport = outports[index];
        if (!outport) continue;
        const auto targets = fed.find(static_cast<int>(index));
        if (targets == fed.end()) continue;

        const Connection* inner = nullptr;
        for (const Connection& wire : innerWires)
            if (wire.targetBlock == outport->id()) {
                inner = &wire;
                break;
            }
        if (!inner) continue;

        Endpoint source;
        const auto throughPort = inportIndex.find(inner->sourceBlock);
        if (throughPort != inportIndex.end()) {
            const auto outside = feeding.find(throughPort->second);
            if (outside == feeding.end()) continue;
            source = outside->second;
        } else {
            const std::string moved = movedId(inner->sourceBlock);
            if (moved.empty()) continue;
            source = {moved, inner->sourcePort};
        }

        Block* driver = model.block(source.block);
        const std::string carried = block->signalName(static_cast<int>(index));
        if (driver && !carried.empty() && driver->signalName(source.port).empty())
            driver->setSignalName(source.port, carried);

        for (const Endpoint& target : targets->second)
            model.connect(source.block, source.port, target.block, target.port);
    }

    model.removeBlock(subsystemId);
    return result;
}

}
