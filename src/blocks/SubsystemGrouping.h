#pragma once

#include "model/FlatModel.h"
#include "model/Model.h"

#include <string>
#include <vector>

namespace simupy {

struct GroupResult {
    std::string subsystemId;
    int inputs = 0;
    int outputs = 0;
};

GroupResult groupIntoSubsystem(Model& model,
                               const std::vector<std::string>& blockIds);

struct UngroupResult {
    std::vector<std::string> blockIds;
    int resolvedParameters = 0;
};

UngroupResult ungroupSubsystem(Model& model, const std::string& subsystemId,
                               const ExpressionEvaluator& evaluate = {});

}
