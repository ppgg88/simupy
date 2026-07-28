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
        SubsystemGrouping.cpp
    PUBLIC SimuPy::simupy-model)
