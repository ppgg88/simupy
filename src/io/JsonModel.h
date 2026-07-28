#pragma once

#include "model/Model.h"

#include <nlohmann/json.hpp>

#include <set>
#include <string>
#include <vector>

namespace simupy {
namespace json_io {

using json = nlohmann::json;

json encodeParam(const ParamValue& value);
ParamValue decodeParam(const json& node);

json encodeGeometry(const BlockGeometry& g);
BlockGeometry decodeGeometry(const json& node);

json encodeParamSpec(const ParamSpec& spec);
ParamSpec decodeParamSpec(const json& node);

json encodeContents(const Model& model,
                    const std::set<std::string>* only = nullptr);

void decodeContents(const json& document, Model& model,
                    std::vector<std::string>* missingTypes = nullptr);

}  // namespace json_io
}  // namespace simupy
