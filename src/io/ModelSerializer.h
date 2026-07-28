#pragma once

#include "model/Model.h"

#include <string>
#include <vector>

namespace simupy {

/// What a load had to work around, for the editor to report afterwards.
struct LoadReport {
    std::vector<std::string> missingTypes;

    struct Requirement {
        std::string library;
        int revision = 0;
    };
    std::vector<Requirement> requires_;

    bool clean() const { return missingTypes.empty(); }
};

/// Reads and writes `.spy` model files.
///
/// The format is plain JSON: readable in a diff, editable by hand, and stable
/// enough to be kept under version control alongside the rest of a project.
///
/// A model is self-contained. Blocks that came from a library are written out
/// with their full contents, so the file opens on a machine that has never
/// seen the library — see LoadReport for what such a load gives up.
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

    /// MIME type for the clipboard. Selections also travel as plain text, so
    /// they can be pasted between two running copies or kept in a scratch file.
    static constexpr const char* kClipboardMimeType =
        "application/x-simupy-blocks";

    static constexpr int kFormatVersion = 1;
};

}  // namespace simupy
