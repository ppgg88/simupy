#pragma once

#include "CustomBlock.h"

#include <string>

namespace simupy {

class LibrarySerializer {
public:
    static std::string toJson(const CustomLibrary& library, int indent = 2);
    static void fromJson(const std::string& json, CustomLibrary& library);

    static void save(const CustomLibrary& library, const std::string& path);
    static void load(CustomLibrary& library, const std::string& path);

    static constexpr int kFormatVersion = 1;

    static constexpr const char* kExtension = "spylib";
};

}
