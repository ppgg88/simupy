#include "FileWrite.h"

#include "model/Types.h"

#include <filesystem>
#include <fstream>

namespace simupy {
namespace {

namespace fs = std::filesystem;

}

void writeFileAtomically(const std::string& path, const std::string& text) {
    const fs::path target(path);

    // Beside the target: a rename is only atomic within one filesystem.
    fs::path temporary = target;
    temporary += ".part";

    {
        std::ofstream stream(temporary,
                             std::ios::out | std::ios::trunc | std::ios::binary);
        if (!stream) throw ModelError("cannot write to '" + path + "'");

        stream << text;
        stream.close();

        if (!stream) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            throw ModelError("failed while writing '" + path + "'");
        }
    }

    std::error_code code;
    fs::rename(temporary, target, code);
    if (code) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw ModelError("could not put '" + path + "' in place: " +
                         code.message());
    }
}

}
