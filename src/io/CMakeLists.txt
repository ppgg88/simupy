simupy_add_library(simupy-io
    SOURCES JsonModel.cpp ModelSerializer.cpp LibrarySerializer.cpp CustomBlock.cpp
            FileWrite.cpp
    PUBLIC SimuPy::simupy-model SimuPy::simupy-blocks SimuPy::simupy-scripting
    PRIVATE nlohmann_json::nlohmann_json)
