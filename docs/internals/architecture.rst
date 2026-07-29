============
Architecture
============

Layers
======

Seven static libraries, one per layer, each depending only on the ones below:

.. code-block:: text

   simupy-model      Types Block BlockRegistry Model FlatModel      Eigen only
     ├── simupy-blocks     the built-in block set
     ├── simupy-engine     Scheduler Solver Simulator SignalLog Pacer
     ├── simupy-scripting  the embedded interpreter and Python blocks  + pybind11
     └── simupy-io         .spy and .spylib, custom block libraries    + nlohmann
           └── simupy-gui  style/ canvas/ panels/ dialogs/ + shell     + Qt
                 simupy, simupy-cli

The graph is acyclic and the build enforces it: ``simupy-model`` does not
compile against pybind11, so the engine is buildable and testable without an
interpreter. CI runs a headless build to keep that true.

.. important::

   Masked parameters are Python expressions, which would otherwise invert the
   layering. The evaluator is injected as a ``std::function`` and supplied by
   the application, not reached for from inside the flattener.

``simupy-io`` sits above blocks and scripting because a library file
instantiates exactly those two kinds of block — a saved subsystem or a saved
Python class.

Source layout
=============

.. code-block:: text

   src/
     model/       the data a diagram is made of, and flattening
     blocks/      the built-in block set
     engine/      scheduling, integration, logging, pacing
     scripting/   the embedded interpreter and Python blocks
     io/          .spy and .spylib, custom block libraries
     app/
       cli/
       gui/
         style/     theme, icons
         canvas/    scene, view, items, routing
         panels/    library, properties, controls, console
         dialogs/   settings, scope, editors, library manager

Headers are included as ``"engine/Simulator.h"`` from anywhere, so the layer a
file belongs to is visible at every use.

``cmake/SimuPyLibrary.cmake`` declares a layer in one call, so warning flags
and the include root are stated once rather than seven times.

Tests
=====

.. code-block:: text

   tests/
     support/     the harness and shared model builders
     core/        one file per domain
     gui/         the offscreen interface tests

No framework: the engine has a narrow surface and the checks are mostly
numerical, so a handful of helpers is enough. Each domain file exposes a
``run…Tests()`` that ``core/main.cpp`` calls.

Both test binaries run with their working directory pinned to the project
root, so they can reach ``examples/``, ``libraries/`` and ``tools/`` without
guessing relative paths.
