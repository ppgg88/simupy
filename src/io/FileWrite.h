#pragma once

#include <string>

namespace simupy {

/// Writes through a temporary, then renames over the target, so a failed
/// write leaves the previous file intact.
void writeFileAtomically(const std::string& path, const std::string& text);

}
