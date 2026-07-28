#include "PythonBlock.h"

#include "blocks/BlockUtils.h"
#include "PythonEngine.h"
#include "PythonError.h"

#include <pybind11/embed.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>

#include <functional>

namespace py = pybind11;

#if defined(__GNUC__) || defined(__clang__)
#define SIMUPY_HIDDEN __attribute__((visibility("hidden")))
#else
#define SIMUPY_HIDDEN
#endif

namespace simupy {
namespace {

py::object viewOf(const double* data, Eigen::Index size, bool writable) {
    if (data == nullptr || size <= 0) {
        // A capsule cannot hold null, and an empty array needs no backing memory.
        py::array_t<double> empty(static_cast<py::ssize_t>(0));
        if (!writable) empty.attr("setflags")(py::arg("write") = false);
        return empty;
    }

    auto keepAlive = py::capsule(const_cast<double*>(data), [](void*) {});
    py::array_t<double> array({size}, {static_cast<Eigen::Index>(sizeof(double))},
                              data, keepAlive);
    if (!writable) array.attr("setflags")(py::arg("write") = false);
    return array;
}

void assign(const py::handle& value, Eigen::Ref<Vec> target,
            const std::string& what) {
    py::array_t<double, py::array::c_style | py::array::forcecast> array;
    try {
        array = py::cast<py::array_t<double, py::array::c_style |
                                                 py::array::forcecast>>(value);
    } catch (const py::cast_error&) {
        throw ModelError(what + " must be a number or an array of numbers");
    }

    const Eigen::Index count = static_cast<Eigen::Index>(array.size());
    const double* source = array.data();

    if (count == target.size()) {
        std::copy(source, source + count, target.data());
    } else if (count == 1) {
        target.setConstant(source[0]);
    } else {
        throw ModelError(what + " returned " + std::to_string(count) +
                         " values but " + std::to_string(target.size()) +
                         " were expected");
    }
}

int intAttribute(const py::object& object, const char* name, int fallback) {
    if (!py::hasattr(object, name)) return fallback;
    py::object value = object.attr(name);
    if (value.is_none()) return fallback;
    return value.cast<int>();
}

std::vector<std::string> portNames(const py::object& object, const char* name,
                                   const std::string& stem, int fallback) {
    if (!py::hasattr(object, name)) return numberedPorts(stem, fallback);

    py::object value = object.attr(name);
    if (value.is_none()) return {};

    if (py::isinstance<py::int_>(value)) {
        const int count = value.cast<int>();
        if (count <= 0) return {};
        if (count == 1) return {stem};
        return numberedPorts(stem, count);
    }

    std::vector<std::string> names;
    for (const py::handle& item : value)
        names.push_back(py::str(item).cast<std::string>());
    return names;
}

}

// Hidden visibility, matching pybind11's, keeps the linker quiet.
struct SIMUPY_HIDDEN PythonBlock::Impl {
    std::string compiledSource;
    std::string compiledClassName;
    py::object blockClass;
    PortLayout layout{{"in"}, {"out"}};

    py::object instance;
    py::object outputFn, derivativeFn, updateFn, zeroCrossingFn;

    Vec stateBuffer, discreteBuffer;
    py::object stateView, discreteView;

    py::object inputList;
    std::vector<const double*> inputPointers;

    int continuousStates = 0;
    int discreteStates = 0;
    int zeroCrossings = 0;
    std::vector<int> outputWidths;

    void releaseInstance() {
        ScopedGil gil;
        outputFn = py::object();
        derivativeFn = py::object();
        updateFn = py::object();
        zeroCrossingFn = py::object();
        instance = py::object();
        inputList = py::object();
        inputPointers.clear();
        stateView = py::object();
        discreteView = py::object();
    }

    void releaseAll() {
        releaseInstance();
        ScopedGil gil;
        blockClass = py::object();
        compiledSource.clear();
    }
};

PythonBlock::PythonBlock() : impl_(std::make_unique<Impl>()) {}

PythonBlock::~PythonBlock() {
    if (PythonEngine::instance().isReady()) {
        try {
            impl_->releaseAll();
        } catch (...) {
        }
    }
}

const char* PythonBlock::defaultSource() {
    return R"(import numpy as np
from simupy import Block

class MyBlock(Block):
    """A first-order lag: y' = (u - y) / tau."""

    inputs = ["u"]
    outputs = ["y"]
    states = 1
    direct_feedthrough = False

    def setup(self, widths):
        self.tau = self.params.get("tau", 0.5)

    def initial_state(self):
        return [0.0]

    def output(self, t, u):
        return self.x

    def derivative(self, t, u):
        return (u[0] - self.x) / self.tau
)";
}

namespace {

py::object resolveBlockClass(const std::string& source,
                             const std::string& requestedName,
                             const std::string& originName) {
    py::dict namespaceDict;
    namespaceDict["__name__"] = originName;
    py::exec(source, namespaceDict);

    py::object base = py::module_::import("simupy").attr("Block");

    if (!requestedName.empty()) {
        if (!namespaceDict.contains(requestedName.c_str()))
            throw ModelError("the script defines no class named '" +
                             requestedName + "'");
        return namespaceDict[requestedName.c_str()];
    }

    std::vector<std::string> derived;
    std::vector<std::string> duckTyped;
    py::module_ builtins = py::module_::import("builtins");
    py::object isclass = py::module_::import("inspect").attr("isclass");

    for (const auto& item : namespaceDict) {
        const std::string key = py::str(item.first).cast<std::string>();
        if (!key.empty() && key[0] == '_') continue;
        py::object value = py::reinterpret_borrow<py::object>(item.second);
        if (!isclass(value).cast<bool>()) continue;
        if (value.is(base)) continue;

        // Only classes the script defines; an imported base would be ambiguous.
        if (py::hasattr(value, "__module__") &&
            py::str(value.attr("__module__")).cast<std::string>() == "simupy")
            continue;

        if (builtins.attr("issubclass")(value, base).cast<bool>())
            derived.push_back(key);
        else if (py::hasattr(value, "output"))
            duckTyped.push_back(key);
    }

    const std::vector<std::string>& candidates =
        derived.empty() ? duckTyped : derived;

    if (candidates.empty())
        throw ModelError(
            "the script defines no block class. Subclass simupy.Block and "
            "implement output().");
    if (candidates.size() > 1) {
        std::string names;
        for (const std::string& name : candidates) {
            if (!names.empty()) names += ", ";
            names += name;
        }
        throw ModelError("the script defines several block classes (" + names +
                         "). Name the one to use in the 'Class name' "
                         "parameter.");
    }
    return namespaceDict[candidates.front().c_str()];
}

py::dict buildParams(const std::string& text) {
    py::dict result;
    if (text.find_first_not_of(" \t\r\n") == std::string::npos) return result;

    py::dict globalsDict;
    globalsDict["__builtins__"] = py::module_::import("builtins");
    globalsDict["np"] = py::module_::import("numpy");
    globalsDict["numpy"] = globalsDict["np"];

    py::dict localsDict;
    py::exec(text, globalsDict, localsDict);

    for (const auto& item : localsDict) {
        const std::string key = py::str(item.first).cast<std::string>();
        if (!key.empty() && key[0] == '_') continue;
        result[item.first] = item.second;
    }
    return result;
}

void mergeDeclaredParams(py::dict& target, const Parameters& params) {
    for (const auto& [key, value] : params.all()) {
        if (key == "code" || key == "className" || key == "parameters") continue;

        if (auto* b = std::get_if<bool>(&value)) target[key.c_str()] = *b;
        else if (auto* d = std::get_if<double>(&value)) target[key.c_str()] = *d;
        else if (auto* s = std::get_if<std::string>(&value)) target[key.c_str()] = *s;
        else if (auto* v = std::get_if<std::vector<double>>(&value)) {
            py::module_ numpy = py::module_::import("numpy");
            target[key.c_str()] = numpy.attr("array")(py::cast(*v));
        }
    }
}

}

PortLayout PythonBlock::ports() const {
    const std::string source = params().text("code", "");
    const std::string className = params().text("className", "");
    const std::string key = className + "\n" + source;

    if (impl_->compiledSource == key && impl_->blockClass) return impl_->layout;
    if (!PythonEngine::instance().isReady()) return impl_->layout;

    ScopedGil gil;
    try {
        py::object blockClass = resolveBlockClass(source, className,
                                                  "<" + name() + ">");
        PortLayout layout;
        layout.inputs = portNames(blockClass, "inputs", "in", 1);
        layout.outputs = portNames(blockClass, "outputs", "out", 1);

        impl_->blockClass = blockClass;
        impl_->layout = layout;
        impl_->compiledSource = key;
        lastError_.clear();
    } catch (const py::error_already_set& error) {
        lastError_ = describe(error);
    } catch (const ModelError& error) {
        lastError_ = error.what();
    }
    return impl_->layout;
}

std::string PythonBlock::validate() const {
    if (!PythonEngine::instance().isReady())
        return "the Python interpreter is not running";

    impl_->compiledSource.clear();
    ports();
    return lastError_;
}

void PythonBlock::setup(BlockSetup& s) {
    if (!PythonEngine::instance().isReady())
        throw ModelError("the Python interpreter is not running");

    impl_->releaseInstance();
    impl_->compiledSource.clear();
    lastError_.clear();

    ScopedGil gil;
    try {
        const std::string source = params().text("code", "");
        const std::string className = params().text("className", "");
        py::object blockClass =
            resolveBlockClass(source, className, "<" + name() + ">");

        impl_->blockClass = blockClass;
        impl_->layout.inputs = portNames(blockClass, "inputs", "in", 1);
        impl_->layout.outputs = portNames(blockClass, "outputs", "out", 1);
        impl_->compiledSource = className + "\n" + source;

        py::object instance = blockClass();
        py::dict blockParams = buildParams(params().text("parameters", ""));
        mergeDeclaredParams(blockParams, params());
        instance.attr("params") = blockParams;

        py::list widths;
        for (int i = 0; i < s.inputCount(); ++i) widths.append(s.inputWidths[i]);

        py::object declared;
        if (py::hasattr(instance, "setup")) declared = instance.attr("setup")(widths);

        impl_->continuousStates = intAttribute(instance, "states", 0);
        impl_->discreteStates = intAttribute(instance, "discrete_states", 0);
        impl_->zeroCrossings = intAttribute(instance, "zero_crossings", 0);
        if (impl_->continuousStates < 0 || impl_->discreteStates < 0 ||
            impl_->zeroCrossings < 0)
            throw ModelError("state and zero-crossing counts cannot be "
                             "negative");

        const double sampleTime =
            py::hasattr(instance, "sample_time")
                ? instance.attr("sample_time").cast<double>()
                : 0.0;
        const bool feedthrough =
            py::hasattr(instance, "direct_feedthrough")
                ? instance.attr("direct_feedthrough").cast<bool>()
                : true;
        logsInputs_ = py::hasattr(instance, "logs_inputs")
                          ? instance.attr("logs_inputs").cast<bool>()
                          : false;

        const int outputCount = s.outputCount();
        impl_->outputWidths.assign(outputCount, s.inheritedWidth());
        if (declared && !declared.is_none()) {
            if (py::isinstance<py::int_>(declared)) {
                const int width = declared.cast<int>();
                if (width > 0)
                    impl_->outputWidths.assign(outputCount, width);
            } else {
                std::vector<int> widthList;
                for (const py::handle& item : declared)
                    widthList.push_back(item.cast<int>());
                if (static_cast<int>(widthList.size()) != outputCount)
                    throw ModelError("setup() returned " +
                                     std::to_string(widthList.size()) +
                                     " widths but the block has " +
                                     std::to_string(outputCount) + " outputs");
                for (int i = 0; i < outputCount; ++i) {
                    if (widthList[i] <= 0)
                        throw ModelError("output width " +
                                         std::to_string(i + 1) +
                                         " must be positive");
                    impl_->outputWidths[i] = widthList[i];
                }
            }
        }

        for (int i = 0; i < outputCount; ++i)
            s.outputWidths[i] = impl_->outputWidths[i];
        s.continuousStates = impl_->continuousStates;
        s.discreteStates = impl_->discreteStates;
        s.zeroCrossings = impl_->zeroCrossings;
        s.sampleTime = sampleTime;
        s.directFeedthrough.assign(s.inputCount(), feedthrough);

        impl_->stateBuffer = Vec::Zero(impl_->continuousStates);
        impl_->discreteBuffer = Vec::Zero(impl_->discreteStates);
        impl_->stateView = viewOf(impl_->stateBuffer.data(),
                                  impl_->stateBuffer.size(), false);
        impl_->discreteView = viewOf(impl_->discreteBuffer.data(),
                                     impl_->discreteBuffer.size(), false);
        instance.attr("x") = impl_->stateView;
        instance.attr("xd") = impl_->discreteView;

        impl_->instance = instance;
        impl_->outputFn = instance.attr("output");
        if (py::hasattr(instance, "derivative"))
            impl_->derivativeFn = instance.attr("derivative");
        if (py::hasattr(instance, "update"))
            impl_->updateFn = instance.attr("update");
        if (py::hasattr(instance, "zero_crossing"))
            impl_->zeroCrossingFn = instance.attr("zero_crossing");

        impl_->inputPointers.clear();
        impl_->inputList = py::list();
    } catch (const ModelError&) {
        throw;
    } catch (const py::error_already_set& error) {
        lastError_ = describe(error);
        throw ModelError(lastError_);
    }
}

void PythonBlock::initialize(Eigen::Ref<Vec> xc, Eigen::Ref<Vec> xd) {
    xc.setZero();
    xd.setZero();
    if (!impl_->instance) return;

    ScopedGil gil;
    try {
        if (xc.size() > 0 && py::hasattr(impl_->instance, "initial_state")) {
            py::object value = impl_->instance.attr("initial_state")();
            if (!value.is_none()) assign(value, xc, "initial_state()");
        }
        if (xd.size() > 0 &&
            py::hasattr(impl_->instance, "initial_discrete_state")) {
            py::object value =
                impl_->instance.attr("initial_discrete_state")();
            if (!value.is_none())
                assign(value, xd, "initial_discrete_state()");
        }
    } catch (const py::error_already_set& error) {
        throw ModelError("block '" + name() + "': " +
                         describe(error));
    }
}

void PythonBlock::computeOutputs(const EvalContext& c) {
    if (!impl_->instance) return;

    ScopedGil gil;
    try {
        bool rebuild = static_cast<int>(impl_->inputPointers.size()) != c.nu;
        for (int i = 0; !rebuild && i < c.nu; ++i)
            rebuild = impl_->inputPointers[i] != c.in(i).data();

        if (rebuild) {
            py::list views;
            impl_->inputPointers.resize(c.nu);
            for (int i = 0; i < c.nu; ++i) {
                impl_->inputPointers[i] = c.in(i).data();
                views.append(viewOf(c.in(i).data(), c.in(i).size(), false));
            }
            impl_->inputList = std::move(views);
        }

        if (impl_->continuousStates > 0)
            std::copy(c.xc, c.xc + impl_->continuousStates,
                      impl_->stateBuffer.data());
        if (impl_->discreteStates > 0)
            std::copy(c.xd, c.xd + impl_->discreteStates,
                      impl_->discreteBuffer.data());

        py::object result = impl_->outputFn(c.t, impl_->inputList);

        if (c.ny == 1) {
            assign(result, c.out(0), "output()");
        } else if (c.ny > 1) {
            if (result.is_none())
                throw ModelError("output() returned None but the block has " +
                                 std::to_string(c.ny) + " outputs");
            py::sequence items = py::reinterpret_borrow<py::sequence>(result);
            if (static_cast<int>(py::len(items)) != c.ny)
                throw ModelError("output() returned " +
                                 std::to_string(py::len(items)) +
                                 " values but the block has " +
                                 std::to_string(c.ny) + " outputs");
            for (int i = 0; i < c.ny; ++i)
                assign(items[i], c.out(i),
                       "output() value " + std::to_string(i + 1));
        }
    } catch (const py::error_already_set& error) {
        throw ModelError("block '" + name() + "' failed in output():\n" +
                         describe(error));
    }
}

void PythonBlock::computeDerivatives(const EvalContext& c,
                                     Eigen::Ref<Vec> dxc) {
    if (!impl_->instance || !impl_->derivativeFn) {
        dxc.setZero();
        return;
    }

    ScopedGil gil;
    try {
        std::copy(c.xc, c.xc + impl_->continuousStates,
                  impl_->stateBuffer.data());
        py::object result = impl_->derivativeFn(c.t, impl_->inputList);
        if (result.is_none())
            dxc.setZero();
        else
            assign(result, dxc, "derivative()");
    } catch (const py::error_already_set& error) {
        throw ModelError("block '" + name() + "' failed in derivative():\n" +
                         describe(error));
    }
}

void PythonBlock::updateDiscrete(const EvalContext& c,
                                 Eigen::Ref<Vec> xdNext) {
    if (!impl_->instance || !impl_->updateFn || impl_->discreteStates == 0) {
        if (xdNext.size() > 0) xdNext = c.xdisc();
        return;
    }

    ScopedGil gil;
    try {
        std::copy(c.xd, c.xd + impl_->discreteStates,
                  impl_->discreteBuffer.data());
        py::object result = impl_->updateFn(c.t, impl_->inputList);
        if (result.is_none())
            xdNext = c.xdisc();
        else
            assign(result, xdNext, "update()");
    } catch (const py::error_already_set& error) {
        throw ModelError("block '" + name() + "' failed in update():\n" +
                         describe(error));
    }
}

void PythonBlock::computeZeroCrossings(const EvalContext& c,
                                       Eigen::Ref<Vec> zc) {
    if (!impl_->instance || !impl_->zeroCrossingFn || zc.size() == 0) {
        zc.setZero();
        return;
    }

    ScopedGil gil;
    try {
        if (impl_->continuousStates > 0)
            std::copy(c.xc, c.xc + impl_->continuousStates,
                      impl_->stateBuffer.data());
        py::object result = impl_->zeroCrossingFn(c.t, impl_->inputList);
        if (result.is_none())
            zc.setZero();
        else
            assign(result, zc, "zero_crossing()");
    } catch (const py::error_already_set& error) {
        throw ModelError("block '" + name() + "' failed in zero_crossing():\n" +
                         describe(error));
    }
}

void PythonBlock::terminate() {
    if (!impl_->instance) return;

    ScopedGil gil;
    try {
        if (py::hasattr(impl_->instance, "terminate"))
            impl_->instance.attr("terminate")();
    } catch (const py::error_already_set& error) {
        PythonEngine::instance().emitOutput(
            "block '" + name() + "' failed in terminate():\n" +
                describe(error) + "\n",
            true);
    }
}

void PythonBlock::reset() {
    if (PythonEngine::instance().isReady()) impl_->releaseInstance();
}

void registerPythonBlocks() {
    registerBlockType<PythonBlock>(
        "PythonFunction", "Python",
        "A block whose behaviour is written in Python. The script is "
        "recompiled at the start of every run.",
        {codeParam("code", "Python source", PythonBlock::defaultSource()),
         textParam("className", "Class name", "",
                   "Leave empty when the script defines a single block class."),
         codeParam("parameters", "Parameters", "tau = 0.5",
                   "Assignments made available to the block as self.params.")},
        110.0, 70.0);
}

}
