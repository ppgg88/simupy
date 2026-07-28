#pragma once

#include <pybind11/pybind11.h>

#include <string>

namespace simupy {

inline std::string describe(const pybind11::error_already_set& error) {
    std::string message = error.what();
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
        message.pop_back();
    return message.empty() ? std::string("unknown Python error") : message;
}

}
