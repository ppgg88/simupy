cmake_minimum_required(VERSION 3.21)
project(SimuPy VERSION 0.0.1 LANGUAGES CXX)

# What the binaries report and the packages are named after, both from here.
set(SIMUPY_VERSION "${PROJECT_VERSION}" CACHE STRING "Version reported and packaged")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(SIMUPY_BUILD_GUI "Build the Qt desktop application" ON)
option(SIMUPY_BUILD_TESTS "Build the test suite" ON)
option(SIMUPY_ENABLE_ASAN
       "Build with the address and undefined-behaviour sanitizers" OFF)

if(SIMUPY_ENABLE_ASAN)
    if(MSVC)
        add_compile_options(/fsanitize=address /Zi)
    else()
        # -g and a kept frame pointer, or a report names no line worth reading.
        add_compile_options(-fsanitize=address,undefined
                            -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

include(cmake/SimuPyLibrary.cmake)

find_package(Eigen3 3.3 REQUIRED NO_MODULE)

find_package(nlohmann_json 3.0 QUIET)
if(NOT nlohmann_json_FOUND)
    # Ubuntu ships the header without a CMake package config in some releases.
    find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp REQUIRED)
    add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
    set_target_properties(nlohmann_json::nlohmann_json PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${NLOHMANN_JSON_INCLUDE_DIR}")
endif()

find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter Development)

if(NOT pybind11_DIR)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -m pybind11 --cmakedir
        OUTPUT_VARIABLE PYBIND11_CMAKE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(PYBIND11_CMAKE_DIR)
        set(pybind11_DIR ${PYBIND11_CMAKE_DIR})
    endif()
endif()
find_package(pybind11 2.6 REQUIRED)

add_subdirectory(src/model)
add_subdirectory(src/blocks)
add_subdirectory(src/engine)
add_subdirectory(src/scripting)
add_subdirectory(src/io)
add_subdirectory(src/app/cli)

if(SIMUPY_BUILD_GUI)
    add_subdirectory(src/app/gui)
endif()

if(SIMUPY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(cmake/Packaging.cmake)

message(STATUS "SimuPy ${PROJECT_VERSION}")
message(STATUS "  Python  : ${Python3_VERSION} (${Python3_EXECUTABLE})")
message(STATUS "  pybind11: ${pybind11_VERSION}")
message(STATUS "  GUI     : ${SIMUPY_BUILD_GUI}")
if(SIMUPY_BUILD_GUI)
    if(Qt6Svg_FOUND)
        message(STATUS "  Icons   : SVG and raster")
    else()
        message(STATUS "  Icons   : raster only (install qt6-svg-dev for SVG)")
    endif()
endif()
