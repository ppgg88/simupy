# The embedded interpreter and the Python block. The only layer that pulls in
# pybind11, which is why the engine can be built without it.

# The Python half of the block API lives in a .py file and is embedded here,
# so it can be linted and highlighted like the Python it is.
set(SIMUPY_API_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/simupy_api.py)
set(SIMUPY_API_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/PythonApi.generated.cpp)

add_custom_command(
    OUTPUT ${SIMUPY_API_GENERATED}
    COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/cmake/embed_text.py
            ${SIMUPY_API_SOURCE} ${SIMUPY_API_GENERATED}
            simupy kSimupyModuleSource
    DEPENDS ${SIMUPY_API_SOURCE} ${PROJECT_SOURCE_DIR}/cmake/embed_text.py
    COMMENT "Embedding simupy_api.py"
    VERBATIM)

simupy_add_library(simupy-scripting
    SOURCES
        PythonEngine.cpp
        PythonBlock.cpp
        PythonModule.cpp
        ${SIMUPY_API_GENERATED}
    PUBLIC SimuPy::simupy-model pybind11::embed)

target_compile_definitions(simupy-scripting PUBLIC SIMUPY_VERSION="${PROJECT_VERSION}")
