#include "BlockRegistry.h"

#include <algorithm>

namespace simupy {

BlockRegistry& BlockRegistry::instance() {
    static BlockRegistry registry;
    return registry;
}

void BlockRegistry::registerType(BlockType type) {
    const std::string key = type.name;
    if (type.displayName.empty()) type.displayName = key;
    if (types_.find(key) == types_.end()) order_.push_back(key);
    types_[key] = std::move(type);
}

void BlockRegistry::unregisterType(const std::string& name) {
    if (types_.erase(name) == 0) return;
    order_.erase(std::remove(order_.begin(), order_.end(), name), order_.end());
}

const BlockType* BlockRegistry::find(const std::string& name) const {
    auto it = types_.find(name);
    return it == types_.end() ? nullptr : &it->second;
}

BlockPtr BlockRegistry::create(const std::string& typeName) const {
    const BlockType* type = find(typeName);
    if (!type) throw ModelError("unknown block type '" + typeName + "'");

    BlockPtr block = type->factory();
    block->setTypeName(typeName);
    block->setName(type->displayName);
    for (const ParamSpec& spec : type->params)
        block->params().set(spec.name, spec.defaultValue);
    return block;
}

std::vector<std::string> BlockRegistry::categories() const {
    std::vector<std::string> result;
    for (const std::string& key : order_) {
        const std::string& category = types_.at(key).category;
        if (std::find(result.begin(), result.end(), category) == result.end())
            result.push_back(category);
    }
    return result;
}

std::vector<const BlockType*> BlockRegistry::inCategory(
    const std::string& category) const {
    std::vector<const BlockType*> result;
    for (const std::string& key : order_) {
        const BlockType& type = types_.at(key);
        if (type.category == category) result.push_back(&type);
    }
    return result;
}

std::vector<const BlockType*> BlockRegistry::all() const {
    std::vector<const BlockType*> result;
    result.reserve(order_.size());
    for (const std::string& key : order_) result.push_back(&types_.at(key));
    return result;
}

}  // namespace simupy
