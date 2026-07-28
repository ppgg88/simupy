#include "PythonEngine.h"

#include <pybind11/embed.h>
#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace {

struct ConsoleStream {
    bool isError = false;

    void write(const std::string& text) {
        if (!text.empty()) simupy::PythonEngine::instance().emitOutput(text, isError);
    }

    void flush() {}
    bool isatty() const { return false; }
    bool writable() const { return true; }
};

}  // namespace

namespace simupy {

void ensurePythonModuleLinked() {
}

}  // namespace simupy

PYBIND11_EMBEDDED_MODULE(simupy, m) {
    m.doc() = "SimuPy block API";
    m.attr("__version__") = SIMUPY_VERSION;

    py::class_<ConsoleStream>(m, "_ConsoleStream")
        .def(py::init<>())
        .def_readwrite("is_error", &ConsoleStream::isError)
        .def("write", &ConsoleStream::write)
        .def("flush", &ConsoleStream::flush)
        .def("isatty", &ConsoleStream::isatty)
        .def("writable", &ConsoleStream::writable);
}
