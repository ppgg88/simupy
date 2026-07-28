# Scheduling, integration and logging. Knows how to run a model, not how to
# read one or how to script one.
simupy_add_library(simupy-engine
    SOURCES Scheduler.cpp Simulator.cpp Solver.cpp SignalLog.cpp RealTimePacer.cpp
    PUBLIC SimuPy::simupy-model)
