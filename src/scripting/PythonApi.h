#pragma once

namespace simupy {

/// The Python half of the block API, from `simupy_api.py`.
///
/// Embedded by the build rather than written inline: MSVC caps a single string
/// literal at 16 kB, and keeping it as a real .py file means it can be linted
/// and read with syntax highlighting.
extern const char* const kSimupyModuleSource;

}  // namespace simupy
