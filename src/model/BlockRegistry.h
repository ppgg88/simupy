#pragma once

#include "Block.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace simupy {

struct BlockType {
    std::string name;         ///< unique key, e.g. "Gain"
    std::string category;     ///< palette group, e.g. "Math"
    std::string displayName;  ///< label in the palette
    std::string description;
    std::vector<ParamSpec> params;
    std::function<BlockPtr()> factory;

    /// Optional hint for the canvas: draws the block wider than the default.
    double defaultWidth = 80.0;
    double defaultHeight = 60.0;
};

/// Process-wide catalogue of block types, built-in and library alike.
class BlockRegistry {
public:
    static BlockRegistry& instance();

    void registerType(BlockType type);

    void unregisterType(const std::string& name);

    const BlockType* find(const std::string& name) const;

    BlockPtr create(const std::string& typeName) const;

    std::vector<std::string> categories() const;
    std::vector<const BlockType*> inCategory(const std::string& category) const;
    std::vector<const BlockType*> all() const;

private:
    BlockRegistry() = default;
    std::map<std::string, BlockType> types_;
    std::vector<std::string> order_;  ///< registration order, for stable palettes
};

void registerBuiltinBlocks();

}  // namespace simupy
