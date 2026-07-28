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

/// Injected: expressions are Python, and this layer must not depend on it.
using ExpressionEvaluator = std::function<ParamValue(
    const std::string& expression,
    const std::map<std::string, ParamValue>& scope,
    const std::string& origin)>;

FlatModel flattenModel(Model& model, const ExpressionEvaluator& evaluate = {});

}
