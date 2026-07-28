#pragma once

#include "model/BlockRegistry.h"

#include <string>
#include <utility>
#include <vector>

namespace simupy {

inline ParamSpec realParam(std::string name, std::string label, double value,
                           std::string tooltip = {}) {
    ParamSpec spec;
    spec.name = std::move(name);
    spec.label = std::move(label);
    spec.kind = ParamSpec::Kind::Real;
    spec.defaultValue = value;
    spec.tooltip = std::move(tooltip);
    return spec;
}

inline ParamSpec intParam(std::string name, std::string label, int value,
                          double minimum = 1, double maximum = 4096,
                          std::string tooltip = {}) {
    ParamSpec spec;
    spec.name = std::move(name);
    spec.label = std::move(label);
    spec.kind = ParamSpec::Kind::Integer;
    spec.defaultValue = static_cast<double>(value);
    spec.minimum = minimum;
    spec.maximum = maximum;
    spec.tooltip = std::move(tooltip);
    return spec;
}

inline ParamSpec boolParam(std::string name, std::string label, bool value,
                           std::string tooltip = {}) {
    ParamSpec spec;
    spec.name = std::move(name);
    spec.label = std::move(label);
    spec.kind = ParamSpec::Kind::Boolean;
    spec.defaultValue = value;
    spec.tooltip = std::move(tooltip);
    return spec;
}

inline ParamSpec textParam(std::string name, std::string label,
                           std::string value, std::string tooltip = {}) {
    ParamSpec spec;
    spec.name = std::move(name);
    spec.label = std::move(label);
    spec.kind = ParamSpec::Kind::Text;
    spec.defaultValue = std::move(value);
    spec.tooltip = std::move(tooltip);
    return spec;
}

inline ParamSpec choiceParam(std::string name, std::string label,
                             std::vector<std::string> choices,
                             std::string value, std::string tooltip = {}) {
    ParamSpec spec;
    spec.name = std::move(name);
    spec.label = std::move(label);
    spec.kind = ParamSpec::Kind::Choice;
    spec.choices = std::move(choices);
    spec.defaultValue = std::move(value);
    spec.tooltip = std::move(tooltip);
    return spec;
}

inline ParamSpec vectorParam(std::string name, std::string label,
                             std::vector<double> value,
                             std::string tooltip = {}) {
    ParamSpec spec;
    spec.name = std::move(name);
    spec.label = std::move(label);
    spec.kind = ParamSpec::Kind::Vector;
    spec.defaultValue = std::move(value);
    spec.tooltip = std::move(tooltip);
    return spec;
}

inline ParamSpec codeParam(std::string name, std::string label,
                           std::string value, std::string tooltip = {}) {
    ParamSpec spec;
    spec.name = std::move(name);
    spec.label = std::move(label);
    spec.kind = ParamSpec::Kind::PythonCode;
    spec.defaultValue = std::move(value);
    spec.tooltip = std::move(tooltip);
    return spec;
}

template <typename BlockClass>
void registerBlockType(std::string name, std::string category,
                       std::string description, std::vector<ParamSpec> params,
                       double width = 80.0, double height = 60.0) {
    BlockType type;
    type.name = std::move(name);
    type.category = std::move(category);
    type.displayName = type.name;
    type.description = std::move(description);
    type.params = std::move(params);
    type.defaultWidth = width;
    type.defaultHeight = height;
    type.factory = [] { return BlockPtr(new BlockClass()); };
    BlockRegistry::instance().registerType(std::move(type));
}

inline std::vector<std::string> numberedPorts(const std::string& stem,
                                              int count) {
    std::vector<std::string> names;
    names.reserve(static_cast<std::size_t>(count));
    for (int i = 1; i <= count; ++i) names.push_back(stem + " " + std::to_string(i));
    return names;
}

/// Broadcasts a parameter vector to `width`: a single value fills every
/// element, otherwise the length must match exactly.
inline Vec broadcast(const std::vector<double>& values, int width,
                     const std::string& what) {
    if (values.empty()) return Vec::Zero(width);
    if (values.size() == 1) return Vec::Constant(width, values[0]);
    if (static_cast<int>(values.size()) != width)
        throw ModelError(what + " has " + std::to_string(values.size()) +
                         " elements but the signal is " +
                         std::to_string(width) + " wide");
    return Eigen::Map<const Vec>(values.data(),
                                 static_cast<Eigen::Index>(values.size()));
}

bool isGoto(const Block* block);
bool isFrom(const Block* block);

std::string signalTagOf(const Block* block);

bool isGlobalGoto(const Block* block);

void registerSourceBlocks();
void registerMathBlocks();
void registerContinuousBlocks();
void registerDiscreteBlocks();
void registerRoutingBlocks();
void registerSinkBlocks();

}  // namespace simupy
