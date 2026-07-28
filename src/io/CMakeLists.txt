# Model and library files. Depends on blocks and scripting because a library
# instantiates exactly those two kinds of block.
simupy_add_library(simupy-io
    SOURCES JsonModel.cpp ModelSerializer.cpp LibrarySerializer.cpp CustomBlock.cpp
    PUBLIC SimuPy::simupy-model SimuPy::simupy-blocks SimuPy::simupy-scripting
    PRIVATE nlohmann_json::nlohmann_json)
