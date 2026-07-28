#pragma once

#include <pybind11/pybind11.h>

#include <string>

namespace simupy {

/// Renders a caught Python exception, traceback included.
///
/// pybind11 fetches and clears the interpreter's error state when it builds
/// `error_already_set`, so the exception object — not PyErr_Occurred() — is
/// the only place the details still exist. The GIL must be held.
inline std::string describe(const pybind11::error_already_set& error) {
    std::string message = error.what();
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
        message.pop_back();
    return message.empty() ? std::string("unknown Python error") : message;
}

}  // namespace simupy
