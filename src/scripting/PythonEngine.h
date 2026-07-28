#pragma once

#include "model/FlatModel.h"

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace simupy {

class PythonEngine {
public:
    static PythonEngine& instance();

    void initialize(const std::vector<std::string>& searchPaths = {});

    void shutdown();

    bool isReady() const { return ready_; }

    std::string version() const { return version_; }

    void addSearchPath(const std::string& path);

    void execute(const std::string& code, const std::string& originName);

    std::string checkSyntax(const std::string& code,
                            const std::string& originName);

    ParamValue evaluateExpression(
        const std::string& expression,
        const std::map<std::string, ParamValue>& variables,
        const std::string& originName);

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

ExpressionEvaluator pythonExpressionEvaluator();

class ScopedGilRelease {
public:
    ScopedGilRelease();
    ~ScopedGilRelease();

    ScopedGilRelease(const ScopedGilRelease&) = delete;
    ScopedGilRelease& operator=(const ScopedGilRelease&) = delete;

private:
    void* state_ = nullptr;
};

void ensurePythonModuleLinked();

}
