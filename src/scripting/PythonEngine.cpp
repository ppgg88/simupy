#include "PythonEngine.h"

#include "PythonApi.h"
#include "PythonError.h"

#include <pybind11/embed.h>
#include <pybind11/pybind11.h>

#include <algorithm>

namespace py = pybind11;

namespace simupy {
namespace {

/// The interpreter is created on the main thread, which then hands the GIL
/// back so worker threads can take it. Kept here rather than in the class to
/// avoid leaking CPython types into the header.
PyThreadState* g_mainThreadState = nullptr;

}  // namespace

ScopedGil::ScopedGil() {
    if (!Py_IsInitialized()) return;
    // PyGILState_Ensure works from any thread, including one CPython has
    // never seen before, which is exactly what the simulation worker is. It
    // also nests cheaply, so blocks can guard themselves even when the run
    // already holds the GIL.
    state_ = static_cast<int>(PyGILState_Ensure());
    held_ = true;
}

ScopedGil::~ScopedGil() {
    if (!held_) return;
    PyGILState_Release(static_cast<PyGILState_STATE>(state_));
}

ScopedGilRelease::ScopedGilRelease() {
    if (!Py_IsInitialized()) return;
    // Only meaningful if this thread actually holds the GIL; asking otherwise
    // would be an error rather than a no-op.
    if (PyGILState_Check()) state_ = PyEval_SaveThread();
}

ScopedGilRelease::~ScopedGilRelease() {
    if (state_) PyEval_RestoreThread(static_cast<PyThreadState*>(state_));
}

PythonEngine& PythonEngine::instance() {
    static PythonEngine engine;
    return engine;
}

PythonEngine::~PythonEngine() {
    // Deliberately does not shut the interpreter down: the singleton outlives
    // main() and finalising CPython that late is not safe.
}

void PythonEngine::initialize(const std::vector<std::string>& searchPaths) {
    if (ready_) {
        for (const std::string& path : searchPaths) addSearchPath(path);
        return;
    }

    ensurePythonModuleLinked();

    py::initialize_interpreter();

    std::string failure;
    {
        py::gil_scoped_acquire gil;
        try {
            py::module_ simupyModule = py::module_::import("simupy");
            py::exec(kSimupyModuleSource, simupyModule.attr("__dict__"));

            py::object streamType = simupyModule.attr("_ConsoleStream");
            py::object out = streamType();
            py::object err = streamType();
            err.attr("is_error") = true;

            py::module_ sys = py::module_::import("sys");
            sys.attr("stdout") = out;
            sys.attr("stderr") = err;

            py::object info = sys.attr("version_info");
            version_ = py::str(info.attr("major")).cast<std::string>() + "." +
                       py::str(info.attr("minor")).cast<std::string>() + "." +
                       py::str(info.attr("micro")).cast<std::string>();

            py::module_::import("numpy");
        } catch (const py::error_already_set& error) {
            failure = error.what();
        } catch (const std::exception& error) {
            failure = error.what();
        }
    }

    // The GIL goes back to the interpreter either way, so worker threads (and
    // a later retry) can take it.
    g_mainThreadState = PyEval_SaveThread();

    if (!failure.empty()) {
        // The interpreter is left running on purpose: finalising it here
        // would tear down state that pybind11 objects created above may still
        // reference during unwinding.
        throw ModelError("could not start the embedded Python interpreter:\n" +
                         failure);
    }

    ready_ = true;
    for (const std::string& path : searchPaths) addSearchPath(path);
}

void PythonEngine::shutdown() {
    if (!ready_) return;
    ready_ = false;

    if (g_mainThreadState) {
        PyEval_RestoreThread(g_mainThreadState);
        g_mainThreadState = nullptr;
    }
    py::finalize_interpreter();
}

void PythonEngine::addSearchPath(const std::string& path) {
    if (!ready_ || path.empty()) return;
    if (std::find(searchPaths_.begin(), searchPaths_.end(), path) !=
        searchPaths_.end())
        return;
    searchPaths_.push_back(path);

    ScopedGil gil;
    try {
        py::module_ sys = py::module_::import("sys");
        py::list sysPath = sys.attr("path");
        for (const py::handle& entry : sysPath)
            if (entry.cast<std::string>() == path) return;
        sysPath.insert(0, path);
    } catch (const py::error_already_set&) {
        emitOutput("could not add '" + path + "' to sys.path\n", true);
        PyErr_Clear();
    }
}

void PythonEngine::execute(const std::string& code,
                           const std::string& originName) {
    if (!ready_)
        throw ModelError("the Python interpreter is not running");
    if (code.empty()) return;

    ScopedGil gil;
    try {
        py::dict namespaceDict;
        namespaceDict["__name__"] = originName;
        py::exec(code, namespaceDict);
    } catch (const py::error_already_set& error) {
        throw ModelError(originName + " failed:\n" + describe(error));
    }
}

std::string PythonEngine::checkSyntax(const std::string& code,
                                      const std::string& originName) {
    if (!ready_) return "the Python interpreter is not running";

    ScopedGil gil;
    try {
        py::module_ builtins = py::module_::import("builtins");
        builtins.attr("compile")(code, originName, "exec");
    } catch (const py::error_already_set& error) {
        return describe(error);
    }
    return {};
}

namespace {

py::object toPython(const ParamValue& value) {
    if (auto* b = std::get_if<bool>(&value)) return py::bool_(*b);
    if (auto* d = std::get_if<double>(&value)) return py::float_(*d);
    if (auto* s = std::get_if<std::string>(&value)) return py::str(*s);
    if (auto* v = std::get_if<std::vector<double>>(&value)) {
        py::list values;
        for (double element : *v) values.append(element);
        return values;
    }
    return py::none();
}

ParamValue fromPython(const py::object& value, const std::string& what) {
    // bool before number: in Python a bool *is* an int, and a mask flag that
    // silently became 1.0 would stop driving a checkbox.
    if (py::isinstance<py::bool_>(value)) return value.cast<bool>();
    if (py::isinstance<py::str>(value)) return value.cast<std::string>();
    if (py::isinstance<py::int_>(value) || py::isinstance<py::float_>(value))
        return value.cast<double>();

    if (py::isinstance<py::sequence>(value) || py::hasattr(value, "__iter__")) {
        std::vector<double> values;
        try {
            for (const py::handle& item : value)
                values.push_back(item.cast<double>());
        } catch (const py::cast_error&) {
            throw ModelError(what + " produced a sequence that is not all "
                                    "numbers");
        }
        return values;
    }

    throw ModelError(what + " produced a " +
                     py::str(value.get_type().attr("__name__"))
                         .cast<std::string>() +
                     ", which is not a number, a string, a flag or a vector");
}

}  // namespace

ParamValue PythonEngine::evaluateExpression(
    const std::string& expression,
    const std::map<std::string, ParamValue>& variables,
    const std::string& originName) {
    if (!ready_)
        throw ModelError("cannot evaluate " + originName +
                         ": the Python interpreter is not running");

    ScopedGil gil;
    try {
        py::dict globalsDict;
        globalsDict["__builtins__"] = py::module_::import("builtins");
        globalsDict["np"] = py::module_::import("numpy");
        globalsDict["numpy"] = globalsDict["np"];
        for (const auto& [name, value] : variables)
            globalsDict[name.c_str()] = toPython(value);

        py::object result = py::eval(expression, globalsDict);
        return fromPython(result, originName);
    } catch (const py::error_already_set& error) {
        throw ModelError(originName + " could not be evaluated:\n" +
                         describe(error));
    }
}

ExpressionEvaluator pythonExpressionEvaluator() {
    return [](const std::string& expression,
              const std::map<std::string, ParamValue>& scope,
              const std::string& origin) {
        return PythonEngine::instance().evaluateExpression(expression, scope,
                                                           origin);
    };
}

void PythonEngine::setOutputHandler(
    std::function<void(const std::string&, bool)> handler) {
    std::lock_guard<std::mutex> lock(handlerMutex_);
    outputHandler_ = std::move(handler);
}

void PythonEngine::emitOutput(const std::string& text, bool isError) {
    std::function<void(const std::string&, bool)> handler;
    {
        std::lock_guard<std::mutex> lock(handlerMutex_);
        handler = outputHandler_;
    }
    if (handler) handler(text, isError);
}

}  // namespace simupy
