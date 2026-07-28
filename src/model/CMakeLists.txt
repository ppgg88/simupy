# The data a diagram is made of, and the flattening that removes its
# hierarchy. Depends on nothing but Eigen: no interpreter, no file format.
simupy_add_library(simupy-model
    SOURCES BlockRegistry.cpp Model.cpp FlatModel.cpp
    PUBLIC Eigen3::Eigen)
