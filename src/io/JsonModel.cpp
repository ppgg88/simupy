#include "JsonModel.h"

#include "model/BlockRegistry.h"
#include "blocks/SubsystemBlock.h"

#include <algorithm>

namespace simupy {
namespace json_io {

json encodeParam(const ParamValue& value) {
    if (auto* d = std::get_if<double>(&value)) return *d;
    if (auto* b = std::get_if<bool>(&value)) return *b;
    if (auto* s = std::get_if<std::string>(&value)) return *s;
    if (auto* v = std::get_if<std::vector<double>>(&value)) return *v;
    return nullptr;
}

ParamValue decodeParam(const json& node) {
    if (node.is_boolean()) return node.get<bool>();
    if (node.is_number()) return node.get<double>();
    if (node.is_string()) return node.get<std::string>();
    if (node.is_array()) {
        std::vector<double> values;
        for (const json& item : node)
            values.push_back(item.is_number() ? item.get<double>() : 0.0);
        return values;
    }
    return 0.0;
}

json encodeGeometry(const BlockGeometry& g) {
    return json{{"x", g.x},
                {"y", g.y},
                {"width", g.width},
                {"height", g.height},
                {"rotation", g.rotation},
                {"mirrored", g.mirrored}};
}

BlockGeometry decodeGeometry(const json& node) {
    BlockGeometry g;
    if (!node.is_object()) return g;
    g.x = node.value("x", 0.0);
    g.y = node.value("y", 0.0);
    g.width = node.value("width", 80.0);
    g.height = node.value("height", 60.0);
    g.rotation = node.value("rotation", 0);
    g.mirrored = node.value("mirrored", false);
    return g;
}

namespace {

const char* paramKindName(ParamSpec::Kind kind) {
    switch (kind) {
        case ParamSpec::Kind::Real: return "real";
        case ParamSpec::Kind::Integer: return "integer";
        case ParamSpec::Kind::Boolean: return "boolean";
        case ParamSpec::Kind::Text: return "text";
        case ParamSpec::Kind::Choice: return "choice";
        case ParamSpec::Kind::Vector: return "vector";
        case ParamSpec::Kind::PythonCode: return "code";
    }
    return "real";
}

ParamSpec::Kind paramKindFromName(const std::string& name) {
    if (name == "integer") return ParamSpec::Kind::Integer;
    if (name == "boolean") return ParamSpec::Kind::Boolean;
    if (name == "text") return ParamSpec::Kind::Text;
    if (name == "choice") return ParamSpec::Kind::Choice;
    if (name == "vector") return ParamSpec::Kind::Vector;
    if (name == "code") return ParamSpec::Kind::PythonCode;
    return ParamSpec::Kind::Real;
}

}  // namespace

json encodeParamSpec(const ParamSpec& spec) {
    json node;
    node["name"] = spec.name;
    node["label"] = spec.label;
    node["kind"] = paramKindName(spec.kind);
    node["default"] = encodeParam(spec.defaultValue);
    if (!spec.choices.empty()) node["choices"] = spec.choices;
    if (!spec.tooltip.empty()) node["tooltip"] = spec.tooltip;
    if (std::isfinite(spec.minimum)) node["minimum"] = spec.minimum;
    if (std::isfinite(spec.maximum)) node["maximum"] = spec.maximum;
    return node;
}

ParamSpec decodeParamSpec(const json& node) {
    ParamSpec spec;
    spec.name = node.value("name", std::string());
    spec.label = node.value("label", spec.name);
    spec.kind = paramKindFromName(node.value("kind", std::string("real")));
    if (node.contains("default")) spec.defaultValue = decodeParam(node["default"]);
    if (node.contains("choices"))
        for (const json& choice : node["choices"])
            if (choice.is_string()) spec.choices.push_back(choice.get<std::string>());
    spec.tooltip = node.value("tooltip", std::string());
    if (node.contains("minimum")) spec.minimum = node["minimum"].get<double>();
    if (node.contains("maximum")) spec.maximum = node["maximum"].get<double>();
    return spec;
}

json encodeContents(const Model& model, const std::set<std::string>* only) {
    auto included = [only](const std::string& blockId) {
        return !only || only->count(blockId) > 0;
    };

    json document;
    json blocks = json::array();
    for (const BlockPtr& block : model.blocks()) {
        if (!included(block->id())) continue;
        json node;
        node["id"] = block->id();
        node["type"] = block->typeName();
        node["name"] = block->name();
        node["geometry"] = encodeGeometry(model.geometry(block->id()));

        json params = json::object();
        for (const auto& [key, value] : block->params().all())
            params[key] = encodeParam(value);
        node["params"] = params;

        if (!block->paramExpressions().empty()) {
            json expressions = json::object();
            for (const auto& [key, text] : block->paramExpressions())
                expressions[key] = text;
            node["expressions"] = std::move(expressions);
        }

        if (!block->signalNames().empty()) {
            json signals = json::object();
            for (const auto& [port, name] : block->signalNames())
                signals[std::to_string(port)] = name;
            node["signals"] = std::move(signals);
        }

        if (const auto* subsystem = dynamic_cast<const SubsystemBlock*>(block.get()))
            node["contents"] = encodeContents(subsystem->contents());

        blocks.push_back(std::move(node));
    }
    document["blocks"] = std::move(blocks);

    json connections = json::array();
    for (const Connection& c : model.connections()) {
        if (!included(c.sourceBlock) || !included(c.targetBlock)) continue;
        json node;
        node["id"] = c.id;
        node["source"] = json{{"block", c.sourceBlock}, {"port", c.sourcePort}};
        node["target"] = json{{"block", c.targetBlock}, {"port", c.targetPort}};
        if (!c.waypoints.empty()) {
            json points = json::array();
            for (const auto& [x, y] : c.waypoints)
                points.push_back(json::array({x, y}));
            node["waypoints"] = std::move(points);
        }
        connections.push_back(std::move(node));
    }
    document["connections"] = std::move(connections);
    return document;
}

namespace {

/// A custom block whose library is missing still carries its contents or
/// its source, so degrade to the matching built-in rather than refuse the
/// file. What is lost is the icon, the mask labels and the palette entry.
std::string substituteType(const json& node) {
    if (node.contains("contents")) return "Subsystem";
    const json& params = node.value("params", json::object());
    if (params.is_object() && params.contains("code")) return "PythonFunction";
    return {};
}

}  // namespace

void decodeContents(const json& document, Model& model,
                    std::vector<std::string>* missingTypes) {
    BlockRegistry& registry = BlockRegistry::instance();

    for (const json& node : document.value("blocks", json::array())) {
        const std::string type = node.value("type", std::string());

        std::string effectiveType = type;
        if (!registry.find(type)) {
            effectiveType = substituteType(node);
            if (effectiveType.empty() || !registry.find(effectiveType))
                throw ModelError("unknown block type '" + type +
                                 "' — the file needs a block library you do "
                                 "not have installed");
            if (missingTypes &&
                std::find(missingTypes->begin(), missingTypes->end(), type) ==
                    missingTypes->end())
                missingTypes->push_back(type);
        }

        BlockPtr block = registry.create(effectiveType);
        block->setTypeName(type);
        block->setId(node.value("id", std::string()));
        if (node.contains("name")) block->setName(node["name"].get<std::string>());

        if (node.contains("params") && node["params"].is_object())
            for (const auto& [key, value] : node["params"].items())
                block->params().set(key, decodeParam(value));

        if (node.contains("expressions") && node["expressions"].is_object())
            for (const auto& [key, value] : node["expressions"].items())
                if (value.is_string())
                    block->setParamExpression(key, value.get<std::string>());

        if (node.contains("signals") && node["signals"].is_object())
            for (const auto& [key, value] : node["signals"].items())
                if (value.is_string())
                    block->setSignalName(std::stoi(key), value.get<std::string>());

        Block* added = model.addBlock(std::move(block),
                                      decodeGeometry(node.value("geometry", json())));

        if (auto* subsystem = dynamic_cast<SubsystemBlock*>(added)) {
            if (node.contains("contents")) {
                subsystem->contents().clear();
                decodeContents(node["contents"], subsystem->contents(),
                               missingTypes);
            }
        }
    }

    for (const json& node : document.value("connections", json::array())) {
        const json& source = node.value("source", json::object());
        const json& target = node.value("target", json::object());
        const std::string sourceBlock = source.value("block", std::string());
        const std::string targetBlock = target.value("block", std::string());
        if (!model.block(sourceBlock) || !model.block(targetBlock))
            continue;  // a wire whose endpoint vanished; drop it quietly

        try {
            const Connection& connection =
                model.connect(sourceBlock, source.value("port", 0), targetBlock,
                              target.value("port", 0));
            if (node.contains("waypoints")) {
                auto& waypoints = const_cast<Connection&>(connection).waypoints;
                for (const json& point : node["waypoints"])
                    if (point.is_array() && point.size() == 2)
                        waypoints.emplace_back(point[0].get<double>(),
                                               point[1].get<double>());
            }
        } catch (const ModelError&) {
        }
    }
}

}  // namespace json_io
}  // namespace simupy
