# The embedded interpreter and the Python block. The only layer that pulls in
# pybind11, which is why the engine can be built without it.
simupy_add_library(simupy-scripting
    SOURCES PythonEngine.cpp PythonBlock.cpp PythonModule.cpp
    PUBLIC SimuPy::simupy-model pybind11::embed)
target_compile_definitions(simupy-scripting PUBLIC SIMUPY_VERSION="${PROJECT_VERSION}")
