#pragma once

#include "model/Model.h"

#include <string>
#include <vector>

namespace simupy {

struct LoadReport {
    std::vector<std::string> missingTypes;

    std::vector<std::string> droppedConnections;

    struct Requirement {
        std::string library;
        int revision = 0;
    };
    std::vector<Requirement> requires_;

    bool clean() const {
        return missingTypes.empty() && droppedConnections.empty();
    }
};

class ModelSerializer {
public:
    static std::string toJson(const Model& model, int indent = 2);

    static void fromJson(const std::string& json, Model& model,
                         LoadReport* report = nullptr);

    static void save(const Model& model, const std::string& path);
    static void load(Model& model, const std::string& path,
                     LoadReport* report = nullptr);

    static void cloneDiagram(const Model& source, Model& target);

    static std::string copySelection(const Model& model,
                                     const std::vector<std::string>& blockIds);

    static std::vector<std::string> pasteInto(Model& model,
                                              const std::string& json,
                                              double dx = 0.0, double dy = 0.0);

    static bool isPastable(const std::string& text);

    static constexpr const char* kClipboardMimeType =
        "application/x-simupy-blocks";

    static constexpr int kFormatVersion = 1;
};

}
