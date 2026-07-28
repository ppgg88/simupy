#include "support/Harness.h"

#include "model/BlockRegistry.h"
#include "scripting/PythonEngine.h"

#include <iostream>

void runEngineTests();
void runModelTests();
void runScriptingTests();
void runIoTests();
void runRuntimeTests();
void runHardwareTests();
void runUnboundedTests();

int main() {
    std::cout << "SimuPy core tests\n\n";

    simupy::registerBuiltinBlocks();

    try {
        simupy::PythonEngine::instance().initialize();
        simupy::PythonEngine::instance().setOutputHandler(
            [](const std::string& text, bool isError) {
                if (isError) std::cerr << text;
            });
        std::cout << "Python "
                  << simupy::PythonEngine::instance().version() << "\n\n";
    } catch (const simupy::ModelError& error) {
        std::cerr << "could not start Python: " << error.what() << '\n';
        return 1;
    }

    runEngineTests();
    runModelTests();
    runScriptingTests();
    runIoTests();
    runRuntimeTests();
    runHardwareTests();
    runUnboundedTests();

    return harness::summary();
}
