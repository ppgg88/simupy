#include "CustomBlock.h"

#include "LibrarySerializer.h"
#include "ModelSerializer.h"
#include "scripting/PythonBlock.h"
#include "model/BlockRegistry.h"
#include "blocks/SubsystemBlock.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace simupy {
namespace {

/// Parameter names a Python block reserves for itself. A mask cannot use
/// them, or the block's own source would be overwritten by a mask value.
const char* const kReservedPythonParams[] = {"code", "className", "parameters"};

bool isReservedPythonParam(const std::string& name) {
    for (const char* reserved : kReservedPythonParams)
        if (name == reserved) return true;
    return false;
}

std::string environmentPath(const char* variable) {
    const char* value = std::getenv(variable);
    return value ? std::string(value) : std::string();
}

std::vector<std::string> splitPathList(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == ':') {
            if (!current.empty()) parts.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}

std::string slugify(const std::string& name) {
    std::string slug;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) slug += static_cast<char>(std::tolower(c));
        else if (!slug.empty() && slug.back() != '-') slug += '-';
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    return slug.empty() ? "library" : slug;
}

}  // namespace

const char* customBlockKindName(CustomBlockKind kind) {
    return kind == CustomBlockKind::Python ? "python" : "subsystem";
}

CustomBlockKind customBlockKindFromName(const std::string& name) {
    return name == "python" ? CustomBlockKind::Python
                            : CustomBlockKind::Subsystem;
}

const char* blockIconKindName(BlockIcon::Kind kind) {
    switch (kind) {
        case BlockIcon::Kind::Svg: return "svg";
        case BlockIcon::Kind::Raster: return "raster";
        case BlockIcon::Kind::Text: return "text";
        case BlockIcon::Kind::None: break;
    }
    return "none";
}

BlockIcon::Kind blockIconKindFromName(const std::string& name) {
    if (name == "svg") return BlockIcon::Kind::Svg;
    if (name == "raster") return BlockIcon::Kind::Raster;
    if (name == "text") return BlockIcon::Kind::Text;
    return BlockIcon::Kind::None;
}

CustomBlockDef* CustomLibrary::find(const std::string& blockName) {
    for (CustomBlockDef& def : blocks)
        if (def.name == blockName) return &def;
    return nullptr;
}

const CustomBlockDef* CustomLibrary::find(const std::string& blockName) const {
    return const_cast<CustomLibrary*>(this)->find(blockName);
}

void registerCustomBlock(const CustomBlockDef& def) {
    BlockType type;
    type.name = def.name;
    type.category = def.category.empty() ? "Custom" : def.category;
    type.displayName = def.displayName.empty() ? def.name : def.displayName;
    type.description = def.description;
    type.params = def.params;  // the mask, and only the mask
    type.defaultWidth = def.defaultWidth;
    type.defaultHeight = def.defaultHeight;

    // Captured by value; contents is a shared_ptr, so a definition edited
    // later registers a fresh factory instead of mutating this one under
    // models already using it.
    type.factory = [def]() -> BlockPtr {
        if (def.kind == CustomBlockKind::Python) {
            BlockPtr block(new PythonBlock());
            block->params().set("code", def.code);
            block->params().set("className", def.className);
            block->params().set("parameters", def.parameterScript);
            return block;
        }

        BlockPtr block(new SubsystemBlock());
        if (def.contents)
            ModelSerializer::cloneDiagram(
                *def.contents, static_cast<SubsystemBlock*>(block.get())->contents());
        return block;
    };

    BlockRegistry::instance().registerType(std::move(type));
}

void captureBlockDefinition(const Block& block, CustomBlockDef& def) {
    if (const auto* subsystem = dynamic_cast<const SubsystemBlock*>(&block)) {
        def.kind = CustomBlockKind::Subsystem;
        def.contents = std::make_shared<Model>();
        ModelSerializer::cloneDiagram(subsystem->contents(), *def.contents);
        return;
    }

    if (dynamic_cast<const PythonBlock*>(&block)) {
        def.kind = CustomBlockKind::Python;
        def.code = block.params().text("code", "");
        def.className = block.params().text("className", "");
        def.parameterScript = block.params().text("parameters", "");
        return;
    }

    throw ModelError("only a Subsystem or a Python block can be saved as a "
                     "custom block; '" + block.name() + "' is a " +
                     block.typeName());
}

LibraryManager& LibraryManager::instance() {
    static LibraryManager manager;
    return manager;
}

std::string LibraryManager::userDirectory() const {
    if (!userDirectory_.empty()) return userDirectory_;

    std::string base = environmentPath("XDG_DATA_HOME");
    if (base.empty()) {
        const std::string home = environmentPath("HOME");
        if (home.empty()) return "libraries";
        base = home + "/.local/share";
    }
    return base + "/simupy/libraries";
}

void LibraryManager::setUserDirectory(std::string path) {
    userDirectory_ = std::move(path);
}

void LibraryManager::addSearchPath(std::string path) {
    if (std::find(extraPaths_.begin(), extraPaths_.end(), path) ==
        extraPaths_.end())
        extraPaths_.push_back(std::move(path));
}

std::vector<std::string> LibraryManager::searchPaths() const {
    std::vector<std::string> paths;
    for (const std::string& path :
         splitPathList(environmentPath("SIMUPY_LIBRARY_PATH")))
        paths.push_back(path);
    for (const std::string& path : extraPaths_) paths.push_back(path);
    paths.push_back(userDirectory());
    return paths;
}

std::vector<std::string> LibraryManager::loadAll() {
    std::vector<std::string> problems;

    for (const std::string& directory : searchPaths()) {
        std::error_code code;
        if (!fs::is_directory(directory, code)) continue;

        std::vector<fs::path> files;
        for (const fs::directory_entry& entry :
             fs::directory_iterator(directory, code)) {
            if (!entry.is_regular_file(code)) continue;
            if (entry.path().extension() != ".spylib") continue;
            files.push_back(entry.path());
        }
        std::sort(files.begin(), files.end());

        for (const fs::path& file : files) {
            try {
                load(file.string());
            } catch (const ModelError& error) {
                problems.push_back(file.string() + ": " + error.what());
            }
        }
    }
    return problems;
}

CustomLibrary& LibraryManager::load(const std::string& path) {
    auto loaded = std::make_unique<CustomLibrary>();
    LibrarySerializer::load(*loaded, path);

    remove(loaded->name, false);

    CustomLibrary& library = *loaded;
    libraries_.push_back(std::move(loaded));
    registerLibrary(library);
    return library;
}

CustomLibrary& LibraryManager::create(const std::string& name) {
    if (library(name))
        throw ModelError("a library named '" + name + "' is already loaded");

    auto created = std::make_unique<CustomLibrary>();
    created->name = name;
    CustomLibrary& library = *created;
    libraries_.push_back(std::move(created));
    save(library);
    return library;
}

void LibraryManager::save(CustomLibrary& library) {
    if (library.path.empty()) {
        const std::string directory = userDirectory();
        std::error_code code;
        fs::create_directories(directory, code);
        library.path = directory + "/" + slugify(library.name) + ".spylib";
    }

    ++library.revision;
    LibrarySerializer::save(library, library.path);
    registerLibrary(library);
}

CustomLibrary& LibraryManager::import(const std::string& path) {
    CustomLibrary probe;
    LibrarySerializer::load(probe, path);

    const std::string directory = userDirectory();
    std::error_code code;
    fs::create_directories(directory, code);

    fs::path destination =
        fs::path(directory) / (slugify(probe.name) + ".spylib");
    if (fs::exists(destination, code) &&
        fs::absolute(destination, code) != fs::absolute(path, code)) {
        for (int i = 2; fs::exists(destination, code); ++i)
            destination = fs::path(directory) /
                          (slugify(probe.name) + "-" + std::to_string(i) +
                           ".spylib");
    }

    if (fs::absolute(destination, code) != fs::absolute(path, code)) {
        fs::copy_file(path, destination,
                      fs::copy_options::overwrite_existing, code);
        if (code)
            throw ModelError("cannot copy the library into " + directory +
                             ": " + code.message());
    }
    return load(destination.string());
}

std::vector<CustomLibrary*> LibraryManager::libraries() {
    std::vector<CustomLibrary*> result;
    result.reserve(libraries_.size());
    for (const std::unique_ptr<CustomLibrary>& library : libraries_)
        result.push_back(library.get());
    return result;
}

CustomLibrary* LibraryManager::library(const std::string& name) {
    for (const std::unique_ptr<CustomLibrary>& library : libraries_)
        if (library->name == name) return library.get();
    return nullptr;
}

void LibraryManager::remove(const std::string& libraryName, bool deleteFile) {
    auto it = std::find_if(libraries_.begin(), libraries_.end(),
                           [&](const std::unique_ptr<CustomLibrary>& library) {
                               return library->name == libraryName;
                           });
    if (it == libraries_.end()) return;

    unregisterLibrary(**it);
    if (deleteFile && !(*it)->path.empty()) {
        std::error_code code;
        fs::remove((*it)->path, code);
    }
    libraries_.erase(it);
}

void LibraryManager::addBlock(CustomLibrary& library, CustomBlockDef def) {
    if (def.name.empty()) throw ModelError("a custom block needs a name");
    if (!isNameAvailable(def.name, library.name))
        throw ModelError("'" + def.name +
                         "' is already the name of another block type");

    for (const ParamSpec& spec : def.params) {
        if (spec.name.empty())
            throw ModelError("every mask parameter needs a name");
        if (def.kind == CustomBlockKind::Python && isReservedPythonParam(spec.name))
            throw ModelError("'" + spec.name +
                             "' is reserved by the Python block itself; give "
                             "the mask parameter another name");
    }

    def.libraryName = library.name;
    if (CustomBlockDef* existing = library.find(def.name)) *existing = std::move(def);
    else library.blocks.push_back(std::move(def));

    save(library);
}

void LibraryManager::removeBlock(CustomLibrary& library,
                                 const std::string& blockName) {
    const auto it = std::find_if(library.blocks.begin(), library.blocks.end(),
                                 [&](const CustomBlockDef& def) {
                                     return def.name == blockName;
                                 });
    if (it == library.blocks.end()) return;

    library.blocks.erase(it);
    owners_.erase(blockName);
    BlockRegistry::instance().unregisterType(blockName);
    save(library);
}

const CustomBlockDef* LibraryManager::definition(
    const std::string& typeName) const {
    const auto owner = owners_.find(typeName);
    if (owner == owners_.end()) return nullptr;

    for (const std::unique_ptr<CustomLibrary>& library : libraries_)
        if (library->name == owner->second) return library->find(typeName);
    return nullptr;
}

bool LibraryManager::isNameAvailable(const std::string& typeName,
                                     const std::string& exceptLibrary) const {
    const auto owner = owners_.find(typeName);
    if (owner != owners_.end()) return owner->second == exceptLibrary;
    return BlockRegistry::instance().find(typeName) == nullptr;
}

void LibraryManager::registerLibrary(CustomLibrary& library) {
    unregisterLibrary(library);

    for (CustomBlockDef& def : library.blocks) {
        def.libraryName = library.name;

        if (BlockRegistry::instance().find(def.name)) {
            // Shadowing a built-in would silently change what existing models
            // mean, so the library loses rather than the engine.
            continue;
        }

        registerCustomBlock(def);
        owners_[def.name] = library.name;
    }
}

void LibraryManager::unregisterLibrary(const CustomLibrary& library) {
    for (auto it = owners_.begin(); it != owners_.end();) {
        if (it->second != library.name) {
            ++it;
            continue;
        }
        BlockRegistry::instance().unregisterType(it->first);
        it = owners_.erase(it);
    }
}

}  // namespace simupy
