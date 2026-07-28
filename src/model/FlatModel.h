#pragma once

#include "Model.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace simupy {

struct FlatBlock {
    Block* block = nullptr;
    std::string path;
};

struct FlatConnection {
    int sourceBlock = 0;
    int sourcePort = 0;
    int targetBlock = 0;
    int targetPort = 0;
};

struct FlatModel {
    std::vector<FlatBlock> blocks;
    std::vector<FlatConnection> connections;
};

/// Computes one masked parameter from an enclosing block's parameters.
///
/// Injected rather than called directly: expressions are Python, and the
/// model layer must not depend on the interpreter. The application supplies
/// simupy::pythonExpressionEvaluator(); a model with no expressions needs
/// none, and one with expressions but no evaluator is reported rather than
/// silently left unresolved.
using ExpressionEvaluator = std::function<ParamValue(
    const std::string& expression,
    const std::map<std::string, ParamValue>& scope,
    const std::string& origin)>;

/// Expands every Subsystem into its contents.
///
/// Inport and Outport blocks are boundary markers with no behaviour of their
/// own, so they are removed and the wires that met at them are joined: a
/// signal entering a subsystem is traced back through the parent, and one
/// leaving it is traced forward from whatever feeds the Outport. Nesting is
/// followed to any depth.
///
/// Throws ModelError if the hierarchy is malformed — a subsystem containing
/// itself, or two Inports claiming the same port number.
FlatModel flattenModel(Model& model, const ExpressionEvaluator& evaluate = {});

}  // namespace simupy
