
function(simupy_add_library name)
    cmake_parse_arguments(ARG "" "" "SOURCES;PUBLIC;PRIVATE" ${ARGN})

    add_library(${name} STATIC ${ARG_SOURCES})
    add_library(SimuPy::${name} ALIAS ${name})

    target_include_directories(${name} PUBLIC ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(${name} PUBLIC ${ARG_PUBLIC})
    target_link_libraries(${name} PRIVATE ${ARG_PRIVATE})
    target_compile_options(${name} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wno-unused-parameter>)
endfunction()
