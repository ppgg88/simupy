#pragma once

#include "model/Model.h"

#include <memory>
#include <string>
#include <vector>

namespace simupy {

enum class CustomBlockKind {
    Subsystem,  ///< a saved diagram, instantiated as a masked Subsystem
    Python,     ///< a saved Python class, instantiated as a Python block
};

const char* customBlockKindName(CustomBlockKind kind);
CustomBlockKind customBlockKindFromName(const std::string& name);

struct BlockIcon {
    enum class Kind {
        None,    ///< fall back to the generic subsystem/Python artwork
        Svg,     ///< SVG source, scaled to the block
        Raster,  ///< PNG/JPEG bytes, scaled to the block
        Text,    ///< a short label drawn centred, the way Gain draws its value
    };

    Kind kind = Kind::None;
    std::string data;

    bool empty() const { return kind == Kind::None || data.empty(); }
};

const char* blockIconKindName(BlockIcon::Kind kind);
BlockIcon::Kind blockIconKindFromName(const std::string& name);

struct CustomBlockDef {
    std::string name;         ///< unique type key, e.g. "PIAntiWindup"
    std::string displayName;  ///< label in the palette
    std::string category = "Custom";
    std::string description;
    CustomBlockKind kind = CustomBlockKind::Subsystem;

    std::vector<ParamSpec> params;

    BlockIcon icon;
    double defaultWidth = 100.0;
    double defaultHeight = 70.0;

    std::shared_ptr<Model> contents;

    std::string code;
    std::string className;
    std::string parameterScript;

    std::string libraryName;
};

class CustomLibrary {
public:
    std::string name = "My Blocks";
    std::string description;
    std::string author;
    int revision = 1;
    /// Where it was loaded from. Empty for a library that has never been saved.
    std::string path;

    std::vector<CustomBlockDef> blocks;

    CustomBlockDef* find(const std::string& blockName);
    const CustomBlockDef* find(const std::string& blockName) const;
};

/// Loaded libraries, and the bridge that turns their blocks into ordinary
/// BlockRegistry entries.
class LibraryManager {
public:
    static LibraryManager& instance();

    std::vector<std::string> searchPaths() const;

    std::string userDirectory() const;
    void setUserDirectory(std::string path);

    /// Extra directories, from `SIMUPY_LIBRARY_PATH` or the command line.
    void addSearchPath(std::string path);

    /// Loads every library found on the search path and registers its blocks.
    /// Returns one message per file that failed: a corrupt library is worth
    /// reporting but never worth refusing to start over.
    std::vector<std::string> loadAll();

    CustomLibrary& load(const std::string& path);

    void save(CustomLibrary& library);

    CustomLibrary& import(const std::string& path);

    CustomLibrary& create(const std::string& name);

    std::vector<CustomLibrary*> libraries();
    CustomLibrary* library(const std::string& name);

    void remove(const std::string& libraryName, bool deleteFile);

    void addBlock(CustomLibrary& library, CustomBlockDef def);
    void removeBlock(CustomLibrary& library, const std::string& blockName);

    const CustomBlockDef* definition(const std::string& typeName) const;

    bool isNameAvailable(const std::string& typeName,
                         const std::string& exceptLibrary = {}) const;

private:
    LibraryManager() = default;

    void registerLibrary(CustomLibrary& library);
    void unregisterLibrary(const CustomLibrary& library);

    std::vector<std::unique_ptr<CustomLibrary>> libraries_;
    std::map<std::string, std::string> owners_;
    std::vector<std::string> extraPaths_;
    std::string userDirectory_;
};

void registerCustomBlock(const CustomBlockDef& def);

void captureBlockDefinition(const Block& block, CustomBlockDef& def);

}  // namespace simupy
