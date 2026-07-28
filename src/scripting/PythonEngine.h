#pragma once

#include "model/FlatModel.h"

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace simupy {

/// Owns the embedded CPython interpreter.
///
/// The interpreter is created once on the main thread. Ownership of the GIL
/// is then released, so worker threads acquire it around the work they do —
/// see ScopedGil. Everything here is safe to call from any thread unless
/// noted otherwise.
class PythonEngine {
public:
    static PythonEngine& instance();

    /// Starts the interpreter. `searchPaths` are prepended to sys.path so
    /// models can import helper modules sitting next to them. Safe to call
    /// more than once; later calls only extend the search path.
    ///
    /// Must be called from the main thread before any other member.
    void initialize(const std::vector<std::string>& searchPaths = {});

    /// Stops the interpreter. After this the engine cannot be restarted —
    /// CPython does not reliably support re-initialisation in one process.
    void shutdown();

    bool isReady() const { return ready_; }

    std::string version() const { return version_; }

    void addSearchPath(const std::string& path);

    /// Executes a snippet in a throwaway namespace. Used for the model's init
    /// script. Throws ModelError carrying the Python traceback.
    void execute(const std::string& code, const std::string& originName);

    std::string checkSyntax(const std::string& code,
                            const std::string& originName);

    /// Evaluates one expression with `variables` in scope and converts the
    /// result to a parameter value.
    ///
    /// This is what makes a masked subsystem configurable: an inner block
    /// writes `kp` or `[1, 2*zeta*wn, wn**2]` and the enclosing block's own
    /// parameters supply the names. NumPy is available as `np`. Throws
    /// ModelError carrying the traceback, or explaining why the result is not
    /// something a parameter can hold.
    ParamValue evaluateExpression(
        const std::string& expression,
        const std::map<std::string, ParamValue>& variables,
        const std::string& originName);

    /// Everything Python writes to stdout/stderr is forwarded here. The
    /// callback may fire from the simulation thread, so implementations must
    /// be thread-safe (the GUI hops to its own thread via a queued signal).
    void setOutputHandler(std::function<void(const std::string&, bool isError)> handler);

    void emitOutput(const std::string& text, bool isError);

private:
    PythonEngine() = default;
    ~PythonEngine();

    bool ready_ = false;
    std::string version_;
    std::vector<std::string> searchPaths_;

    std::mutex handlerMutex_;
    std::function<void(const std::string&, bool)> outputHandler_;
};

/// RAII guard that acquires the GIL for the current scope.
///
/// Hoist this as high as you reasonably can: acquiring per block evaluation
/// costs far more than the block itself. The simulation controller holds one
/// for the whole run.
class ScopedGil {
public:
    ScopedGil();
    ~ScopedGil();

    ScopedGil(const ScopedGil&) = delete;
    ScopedGil& operator=(const ScopedGil&) = delete;

private:
    int state_ = 0;
    bool held_ = false;
};

/// The evaluator the model layer needs for masked parameters, backed by the
/// embedded interpreter.
ExpressionEvaluator pythonExpressionEvaluator();

/// Hands the GIL back for the duration of a scope, and takes it again on the
/// way out.
///
/// The inverse of ScopedGil, and the reason hardware blocks work: a run holds
/// the GIL from start to finish, so a Python thread doing serial or socket I/O
/// would only ever be scheduled during the brief moments a block's own code is
/// executing. Releasing it around the parts that do not touch Python — chiefly
/// the wait in a real-time run, which is most of the wall clock — gives those
/// threads the time they need.
///
/// Safe to nest inside a ScopedGil, which is the only way it is ever used.
/// Hands the GIL back for a scope. Nests inside ScopedGil, and is what
/// lets a hardware block's reader thread run while a paced run waits.
class ScopedGilRelease {
public:
    ScopedGilRelease();
    ~ScopedGilRelease();

    ScopedGilRelease(const ScopedGilRelease&) = delete;
    ScopedGilRelease& operator=(const ScopedGilRelease&) = delete;

private:
    void* state_ = nullptr;
};

/// Pulls the translation unit defining the embedded `simupy` module into the
/// link. Without a reference the linker drops the object file from the static
/// library and the module never registers itself.
void ensurePythonModuleLinked();

}  // namespace simupy
