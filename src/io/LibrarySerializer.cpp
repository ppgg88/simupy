#include "LibrarySerializer.h"

#include "JsonModel.h"

#include <fstream>
#include <sstream>

using json = nlohmann::json;
using namespace simupy::json_io;

namespace simupy {
namespace {

constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encodeBase64(const std::string& bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);

    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const unsigned value = (static_cast<unsigned char>(bytes[i]) << 16) |
                               (static_cast<unsigned char>(bytes[i + 1]) << 8) |
                               static_cast<unsigned char>(bytes[i + 2]);
        out += kAlphabet[(value >> 18) & 0x3f];
        out += kAlphabet[(value >> 12) & 0x3f];
        out += kAlphabet[(value >> 6) & 0x3f];
        out += kAlphabet[value & 0x3f];
    }

    if (i < bytes.size()) {
        unsigned value = static_cast<unsigned char>(bytes[i]) << 16;
        const bool two = i + 1 < bytes.size();
        if (two) value |= static_cast<unsigned char>(bytes[i + 1]) << 8;
        out += kAlphabet[(value >> 18) & 0x3f];
        out += kAlphabet[(value >> 12) & 0x3f];
        out += two ? kAlphabet[(value >> 6) & 0x3f] : '=';
        out += '=';
    }
    return out;
}

int base64Value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::string decodeBase64(const std::string& text) {
    std::string out;
    out.reserve(text.size() / 4 * 3);

    unsigned buffer = 0;
    int bits = 0;
    for (char c : text) {
        const int value = base64Value(c);
        if (value < 0) continue;
        buffer = (buffer << 6) | static_cast<unsigned>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buffer >> bits) & 0xff);
        }
    }
    return out;
}

json encodeIcon(const BlockIcon& icon) {
    json node;
    node["kind"] = blockIconKindName(icon.kind);
    if (icon.kind == BlockIcon::Kind::Text) node["text"] = icon.data;
    else if (!icon.data.empty()) node["data"] = encodeBase64(icon.data);
    return node;
}

BlockIcon decodeIcon(const json& node) {
    BlockIcon icon;
    if (!node.is_object()) return icon;
    icon.kind = blockIconKindFromName(node.value("kind", std::string("none")));
    if (icon.kind == BlockIcon::Kind::Text)
        icon.data = node.value("text", std::string());
    else if (node.contains("data") && node["data"].is_string())
        icon.data = decodeBase64(node["data"].get<std::string>());
    return icon;
}

json encodeBlock(const CustomBlockDef& def) {
    json node;
    node["name"] = def.name;
    node["displayName"] = def.displayName;
    node["category"] = def.category;
    node["description"] = def.description;
    node["kind"] = customBlockKindName(def.kind);
    node["width"] = def.defaultWidth;
    node["height"] = def.defaultHeight;

    json params = json::array();
    for (const ParamSpec& spec : def.params) params.push_back(encodeParamSpec(spec));
    node["params"] = std::move(params);

    if (!def.icon.empty()) node["icon"] = encodeIcon(def.icon);

    if (def.kind == CustomBlockKind::Subsystem) {
        node["contents"] = def.contents ? encodeContents(*def.contents)
                                        : json::object();
    } else {
        node["code"] = def.code;
        if (!def.className.empty()) node["className"] = def.className;
        if (!def.parameterScript.empty()) node["parameters"] = def.parameterScript;
    }
    return node;
}

CustomBlockDef decodeBlock(const json& node) {
    CustomBlockDef def;
    def.name = node.value("name", std::string());
    if (def.name.empty()) throw ModelError("a block in this library has no name");

    def.displayName = node.value("displayName", def.name);
    def.category = node.value("category", std::string("Custom"));
    def.description = node.value("description", std::string());
    def.kind = customBlockKindFromName(node.value("kind", std::string("subsystem")));
    def.defaultWidth = node.value("width", 100.0);
    def.defaultHeight = node.value("height", 70.0);

    for (const json& spec : node.value("params", json::array())) {
        ParamSpec decoded = decodeParamSpec(spec);
        if (!decoded.name.empty()) def.params.push_back(std::move(decoded));
    }

    if (node.contains("icon")) def.icon = decodeIcon(node["icon"]);

    if (def.kind == CustomBlockKind::Subsystem) {
        def.contents = std::make_shared<Model>();
        if (node.contains("contents"))
            decodeContents(node["contents"], *def.contents, nullptr);
    } else {
        def.code = node.value("code", std::string());
        def.className = node.value("className", std::string());
        def.parameterScript = node.value("parameters", std::string());
    }
    return def;
}

}

std::string LibrarySerializer::toJson(const CustomLibrary& library, int indent) {
    json document;
    document["format"] = "simupy-library";
    document["version"] = kFormatVersion;
    document["name"] = library.name;
    document["revision"] = library.revision;
    if (!library.description.empty()) document["description"] = library.description;
    if (!library.author.empty()) document["author"] = library.author;

    json blocks = json::array();
    for (const CustomBlockDef& def : library.blocks)
        blocks.push_back(encodeBlock(def));
    document["blocks"] = std::move(blocks);

    return indent < 0 ? document.dump() : document.dump(indent);
}

void LibrarySerializer::fromJson(const std::string& text, CustomLibrary& library) {
    json document;
    try {
        document = json::parse(text);
    } catch (const json::exception& error) {
        throw ModelError(std::string("this is not a valid library file: ") +
                         error.what());
    }

    if (document.value("format", std::string()) != "simupy-library")
        throw ModelError("this file is not a SimuPy block library");

    const int version = document.value("version", 0);
    if (version > kFormatVersion)
        throw ModelError("this library was written by a newer version of "
                         "SimuPy (format " + std::to_string(version) + ")");

    library.blocks.clear();
    library.name = document.value("name", std::string("Untitled library"));
    library.revision = document.value("revision", 1);
    library.description = document.value("description", std::string());
    library.author = document.value("author", std::string());

    decoding("this library file", [&] {
        for (const json& node : document.value("blocks", json::array())) {
            CustomBlockDef def = decodeBlock(node);
            def.libraryName = library.name;
            library.blocks.push_back(std::move(def));
        }
    });
}

void LibrarySerializer::save(const CustomLibrary& library,
                             const std::string& path) {
    std::ofstream stream(path, std::ios::out | std::ios::trunc);
    if (!stream) throw ModelError("cannot write to '" + path + "'");
    stream << toJson(library);
    if (!stream) throw ModelError("failed while writing '" + path + "'");
}

void LibrarySerializer::load(CustomLibrary& library, const std::string& path) {
    std::ifstream stream(path);
    if (!stream) throw ModelError("cannot open '" + path + "'");
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    fromJson(buffer.str(), library);
    library.path = path;
}

}
