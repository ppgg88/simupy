simupy_add_library(simupy-engine
    SOURCES Scheduler.cpp Simulator.cpp Solver.cpp SignalLog.cpp RealTimePacer.cpp
    PUBLIC SimuPy::simupy-model)
