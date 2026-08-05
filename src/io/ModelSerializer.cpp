#include "ModelSerializer.h"

#include "CustomBlock.h"
#include "blocks/SubsystemBlock.h"
#include "FileWrite.h"
#include "JsonModel.h"

#include <fstream>
#include <map>
#include <set>
#include <sstream>

using json = nlohmann::json;
using namespace simupy::json_io;

namespace simupy {
namespace {

json encodeSolver(const SolverSettings& s) {
    return json{{"method", SolverSettings::methodName(s.method)},
                {"startTime", s.startTime},
                {"stopTime", s.stopTime},
                {"fixedStep", s.fixedStep},
                {"maxStep", s.maxStep},
                {"minStep", s.minStep},
                {"initialStep", s.initialStep},
                {"relTol", s.relTol},
                {"absTol", s.absTol},
                {"maxLoggedSamples", s.maxLoggedSamples},
                {"realTime", s.realTime},
                {"realTimeFactor", s.realTimeFactor},
                {"unbounded", s.unbounded}};
}

void decodeSolver(const json& node, SolverSettings& s) {
    if (!node.is_object()) return;
    s.method = SolverSettings::methodFromName(
        node.value("method", std::string("rk45")));
    s.startTime = node.value("startTime", s.startTime);
    s.stopTime = node.value("stopTime", s.stopTime);
    s.fixedStep = node.value("fixedStep", s.fixedStep);
    s.maxStep = node.value("maxStep", s.maxStep);
    s.minStep = node.value("minStep", s.minStep);
    s.initialStep = node.value("initialStep", s.initialStep);
    s.relTol = node.value("relTol", s.relTol);
    s.absTol = node.value("absTol", s.absTol);
    s.maxLoggedSamples = node.value("maxLoggedSamples", s.maxLoggedSamples);
    s.realTime = node.value("realTime", s.realTime);
    s.realTimeFactor = node.value("realTimeFactor", s.realTimeFactor);
    s.unbounded = node.value("unbounded", s.unbounded);
}

void collectLibraries(const Model& model, std::map<std::string, int>& used) {
    const LibraryManager& manager = LibraryManager::instance();
    for (const BlockPtr& block : model.blocks()) {
        if (const CustomBlockDef* def = manager.definition(block->typeName())) {
            const CustomLibrary* library =
                const_cast<LibraryManager&>(manager).library(def->libraryName);
            used[def->libraryName] = library ? library->revision : 0;
        }
        if (const auto* subsystem = dynamic_cast<const SubsystemBlock*>(block.get()))
            collectLibraries(subsystem->contents(), used);
    }
}

}

std::string ModelSerializer::toJson(const Model& model, int indent) {
    json document = encodeContents(model);
    document["format"] = "simupy-model";
    document["version"] = kFormatVersion;
    document["name"] = model.name();
    document["solver"] = encodeSolver(model.solver());
    if (!model.initScript().empty())
        document["initScript"] = model.initScript();

    std::map<std::string, int> used;
    collectLibraries(model, used);
    if (!used.empty()) {
        json requires_ = json::array();
        for (const auto& [name, revision] : used)
            requires_.push_back(json{{"library", name}, {"revision", revision}});
        document["requires"] = std::move(requires_);
    }

    return indent < 0 ? document.dump() : document.dump(indent);
}

void ModelSerializer::fromJson(const std::string& text, Model& model,
                               LoadReport* report) {
    json document;
    try {
        document = json::parse(text);
    } catch (const json::exception& error) {
        throw ModelError(std::string("this is not a valid model file: ") +
                         error.what());
    }

    if (document.value("format", std::string()) != "simupy-model")
        throw ModelError("this file is not a SimuPy model");

    const int version = document.value("version", 0);
    if (version > kFormatVersion)
        throw ModelError("this model was written by a newer version of SimuPy "
                         "(format " + std::to_string(version) + ")");

    model.clear();
    model.setName(document.value("name", std::string("untitled")));
    model.setInitScript(document.value("initScript", std::string()));
    if (document.contains("solver")) decodeSolver(document["solver"], model.solver());

    DecodeReport decoded;
    decoding("this model file",
             [&] { decodeContents(document, model, &decoded); });

    if (report) {
        report->missingTypes = std::move(decoded.missingTypes);
        report->droppedConnections = std::move(decoded.droppedConnections);
        for (const json& node : document.value("requires", json::array())) {
            LoadReport::Requirement requirement;
            requirement.library = node.value("library", std::string());
            requirement.revision = node.value("revision", 0);
            if (!requirement.library.empty())
                report->requires_.push_back(std::move(requirement));
        }
    }
}

void ModelSerializer::save(const Model& model, const std::string& path) {
    writeFileAtomically(path, toJson(model));
}

void ModelSerializer::load(Model& model, const std::string& path,
                           LoadReport* report) {
    std::ifstream stream(path);
    if (!stream) throw ModelError("cannot open '" + path + "'");
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    fromJson(buffer.str(), model, report);
}

void ModelSerializer::cloneDiagram(const Model& source, Model& target) {
    target.clear();
    decodeContents(encodeContents(source), target, nullptr);
}

namespace {

constexpr const char* kSelectionFormat = "simupy-selection";

std::string freeId(const Model& model, const std::string& wanted,
                   const std::set<std::string>& taken) {
    if (!model.block(wanted) && taken.count(wanted) == 0) return wanted;
    for (int i = 1;; ++i) {
        const std::string candidate = wanted + "-" + std::to_string(i);
        if (!model.block(candidate) && taken.count(candidate) == 0)
            return candidate;
    }
}

}

std::string ModelSerializer::copySelection(
    const Model& model, const std::vector<std::string>& blockIds) {
    std::set<std::string> selected(blockIds.begin(), blockIds.end());

    json document = encodeContents(model, &selected);
    document["format"] = kSelectionFormat;
    document["version"] = kFormatVersion;
    return document.dump(2);
}

bool ModelSerializer::isPastable(const std::string& text) {
    if (text.empty()) return false;
    try {
        const json document = json::parse(text);
        return document.value("format", std::string()) == kSelectionFormat;
    } catch (const json::exception&) {
        return false;
    }
}

std::vector<std::string> ModelSerializer::pasteInto(Model& model,
                                                    const std::string& text,
                                                    double dx, double dy) {
    json document;
    try {
        document = json::parse(text);
    } catch (const json::exception& error) {
        throw ModelError(std::string("this is not something to paste: ") +
                         error.what());
    }

    if (document.value("format", std::string()) != kSelectionFormat)
        throw ModelError("this is not a copied block selection");

    std::vector<std::string> created;
    decoding("the pasted selection", [&] {
        std::map<std::string, std::string> remapped;
        std::set<std::string> taken;
        for (json& node : document["blocks"]) {
            const std::string original = node.value("id", std::string());
            const std::string fresh =
                freeId(model, original.empty() ? "block" : original, taken);
            remapped[original] = fresh;
            taken.insert(fresh);
            node["id"] = fresh;

            if (dx != 0.0 || dy != 0.0) {
                json& geometry = node["geometry"];
                geometry["x"] = geometry.value("x", 0.0) + dx;
                geometry["y"] = geometry.value("y", 0.0) + dy;
            }
        }

        if (document.contains("connections")) {
            for (json& node : document["connections"]) {
                node["id"] = freeId(model, "wire", taken);
                taken.insert(node["id"].get<std::string>());
                node["source"]["block"] =
                    remapped[node["source"].value("block", std::string())];
                node["target"]["block"] =
                    remapped[node["target"].value("block", std::string())];
            }
        }

        decodeContents(document, model, nullptr);

        created.reserve(remapped.size());
        for (const json& node : document["blocks"])
            created.push_back(node.value("id", std::string()));
    });
    return created;
}

}
