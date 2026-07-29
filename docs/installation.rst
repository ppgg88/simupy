============
Installation
============

From a release
==============

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Platform
     - Install
   * - Debian, Ubuntu
     - ``sudo apt install ./simupy_*_amd64.deb`` — Qt, Python and NumPy come
       in as dependencies.
   * - Any Linux
     - Unpack the ``.tar.gz``. Needs Qt 6.2+, Python 3.8+ and NumPy already
       present.
   * - Windows
     - Run the ``.exe`` installer, or unpack the ``.zip`` to run in place. Qt
       and the Python runtime are bundled; install NumPy with
       ``pip install numpy``.

``pyserial`` is only needed for the Arduino and serial blocks — see
:doc:`guide/hardware`.

Building from source
====================

Requirements
------------

All are distribution packages on Ubuntu 24.04:

.. list-table::
   :header-rows: 1
   :widths: 24 20 56

   * - Dependency
     - Minimum
     - Ubuntu package
   * - CMake
     - 3.21
     - ``cmake``
   * - C++ compiler
     - C++17
     - ``g++``
   * - Qt
     - 6.2 (Widgets, Charts)
     - ``qt6-base-dev qt6-charts-dev``
   * - Eigen
     - 3.3
     - ``libeigen3-dev``
   * - nlohmann/json
     - 3.0
     - ``nlohmann-json3-dev``
   * - Python
     - 3.8 + headers
     - ``python3-dev``
   * - pybind11
     - 2.6
     - ``python3-pybind11``
   * - NumPy
     - any
     - ``python3-numpy``
   * - Qt SVG *(optional)*
     - 6.2
     - ``qt6-svg-dev``

Qt SVG is only used for vector icons on custom blocks. Without it the build
succeeds and PNG/JPEG icons still work; CMake reports which of the two you got.

Building
--------

.. code-block:: console

   $ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   $ cmake --build build -j$(nproc)
   $ ./build/bin/simupy examples/pid_control_loop.spy

.. list-table::
   :header-rows: 1
   :widths: 34 66

   * - Option
     - Effect
   * - ``-DSIMUPY_BUILD_GUI=OFF``
     - Builds only the engine, the command-line runner and the tests. Useful
       on a headless machine, and what the CI uses to prove the engine still
       compiles without Qt.
   * - ``-DSIMUPY_BUILD_TESTS=OFF``
     - Skips the test suite.

Checking the build
------------------

.. code-block:: console

   $ ctest --test-dir build --output-on-failure

``simupy-tests`` covers the engine: solver order of accuracy against
closed-form solutions, event location, algebraic-loop solving, stiff
integration, signal-width propagation, discrete sample timing, subsystem
flattening, wireless-link resolution and scoping, mask parameter binding,
library round-tripping, copy and paste, real-time pacing, unbounded runs and
the log decimation that makes them possible, interactive controls, the
hardware block lifecycle over a real UDP socket, the Arduino bridge against a
board simulated on a pseudo-terminal, and the Python bridge.

``simupy-gui-tests`` drives the real window offscreen — including synthesised
key and mouse events — to check hierarchy navigation, the editing operations,
signal naming, type-to-insert, driving a slider across the thread boundary
into a live run, and that the canvas shortcuts do not follow the focus into a
text field.

Packaging
=========

.. code-block:: console

   $ cd build && cpack -G "TGZ;DEB"     # Linux
   $ cd build && cpack -G "ZIP;NSIS"    # Windows

The ``.deb`` declares its dependencies, Qt and NumPy included, because the
application embeds CPython and will not run a Python block without it. The
Windows packages bundle Qt and the Python DLL instead; NumPy still has to be
installed with pip.

Continuous integration
----------------------

Tagging ``v*`` runs ``.github/workflows/release.yml``: build, test and package
on both platforms, then attach the assets and their checksums to a GitHub
release.

Every other push runs ``ci.yml`` — the same build and tests on Linux and
Windows, a headless build to prove the engine still compiles without Qt, and a
check that the generated examples and libraries match their generators.
