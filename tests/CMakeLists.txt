add_library(simupy-test-support STATIC
    support/Harness.cpp
    support/Models.cpp)
target_include_directories(simupy-test-support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(simupy-test-support PUBLIC
    SimuPy::simupy-engine SimuPy::simupy-blocks SimuPy::simupy-scripting)

add_executable(simupy-tests
    core/main.cpp
    core/test_engine.cpp
    core/test_model.cpp
    core/test_scripting.cpp
    core/test_io.cpp
    core/test_runtime.cpp
    core/test_hardware.cpp
    core/test_unbounded.cpp)
target_link_libraries(simupy-tests PRIVATE
    simupy-test-support SimuPy::simupy-io)
add_test(NAME simupy-tests COMMAND simupy-tests
         WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

if(SIMUPY_BUILD_GUI)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)

    add_executable(simupy-gui-tests gui/test_gui.cpp)
    target_link_libraries(simupy-gui-tests PRIVATE SimuPy::simupy-gui Qt6::Test)
    add_test(NAME simupy-gui-tests COMMAND simupy-gui-tests
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(simupy-gui-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
