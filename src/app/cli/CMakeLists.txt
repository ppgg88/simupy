add_executable(simupy-cli main.cpp)
target_link_libraries(simupy-cli PRIVATE
    SimuPy::simupy-engine SimuPy::simupy-io SimuPy::simupy-blocks
    SimuPy::simupy-scripting)
