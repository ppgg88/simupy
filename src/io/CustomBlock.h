#pragma once

#include "model/Model.h"
#include "scripting/PythonPackages.h"

#include <memory>
#include <string>
#include <vector>

namespace simupy {

enum class CustomBlockKind {
    Subsystem,
    Python,
};

const char* customBlockKindName(CustomBlockKind kind);
CustomBlockKind customBlockKindFromName(const std::string& name);

struct BlockIcon {
    enum class Kind {
        None,
        Svg,
        Raster,
        Text,
    };

    Kind kind = Kind::None;
    std::string data;

    bool empty() const { return kind == Kind::None || data.empty(); }
};

const char* blockIconKindName(BlockIcon::Kind kind);
BlockIcon::Kind blockIconKindFromName(const std::string& name);

struct CustomBlockDef {
    std::string name;
    std::string displayName;
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
    std::string path;

    std::vector<CustomBlockDef> blocks;

    /// Python packages the blocks import. Declared by the author so a missing
    /// one is named before a run reaches the import.
    std::vector<PackageRequirement> requires_;

    CustomBlockDef* find(const std::string& blockName);
    const CustomBlockDef* find(const std::string& blockName) const;
};

class LibraryManager {
public:
    static LibraryManager& instance();

    std::vector<std::string> searchPaths() const;

    std::string userDirectory() const;
    void setUserDirectory(std::string path);

    void addSearchPath(std::string path);

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

}
