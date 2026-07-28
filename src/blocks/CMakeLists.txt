# The built-in block set.
simupy_add_library(simupy-blocks
    SOURCES
        BuiltinBlocks.cpp
        ContinuousBlocks.cpp
        ControlBlocks.cpp
        DiscreteBlocks.cpp
        MathBlocks.cpp
        RoutingBlocks.cpp
        SinkBlocks.cpp
        SourceBlocks.cpp
        SubsystemBlock.cpp
    PUBLIC SimuPy::simupy-model)
