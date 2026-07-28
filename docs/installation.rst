============
Installation
============

From a release
==============

Releases ship a single Linux artefact: a Flatpak bundle.

.. code-block:: console

   $ flatpak install --user ./simupy-*-x86_64.flatpak
   $ flatpak run io.github.ppgg88.SimuPy

It needs Flatpak with the `Flathub <https://flathub.org/setup>`_ remote
configured, because the KDE 6.9 runtime that provides Qt comes from there.
Everything else — Python 3.12, NumPy, ``pyserial`` — is inside the bundle or
its runtime, so nothing has to be installed alongside.

The sandbox is given your home directory (models are your files), the network
(the UDP blocks) and device access (the serial and Arduino blocks; see
:doc:`guide/hardware`). If you never touch hardware, take the last one back
with ``flatpak override --user --nodevice=all io.github.ppgg88.SimuPy``.

The command-line runner travels in the same bundle:

.. code-block:: console

   $ flatpak run --command=simupy-cli io.github.ppgg88.SimuPy run model.spy

Windows packaging is not built at the moment. Building from source works
there as it always did, but nothing is published for it.

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

The Flatpak
-----------

.. code-block:: console

   $ flatpak install flathub org.flatpak.Builder
   $ flatpak run org.flatpak.Builder --user --force-clean --install \
         build-dir packaging/flatpak/io.github.ppgg88.SimuPy.yml

The manifest builds against the KDE 6.9 runtime, which supplies Qt. Eigen,
nlohmann/json and pybind11 are built from pinned source archives and thrown
away after the compile; NumPy and ``pyserial`` are installed from pinned
wheels, because the build sandbox has no network and nothing may be resolved
while it runs.

.. note::

   ``flatpak-builder`` running inside its own sandbox has a private ``/tmp``.
   Keep the build directory, the state directory and the repository somewhere
   under ``$HOME``, or the build fails with *"build directory not
   initialized"*.

Distribution packages
---------------------

CPack still produces a tarball and a Debian package from the same install
tree, and nothing in the release pipeline uses them:

.. code-block:: console

   $ cd build && cpack -G "TGZ;DEB"

The ``.deb`` declares its dependencies, Qt and NumPy included, because the
application embeds CPython and will not run a Python block without it.

Continuous integration
----------------------

Every push runs ``.github/workflows/ci.yml``: build and test on Ubuntu 24.04,
a headless build to prove the engine still compiles without Qt, the Flatpak
built and then *installed and run*, the documentation built with warnings as
errors, and a check that the generated examples and libraries still match
their generators.

Tagging ``v*`` runs ``release.yml``: the test suite, then the version stamped
onto the manifest and the AppStream data by ``tools/stamp_version.py``, then
the same Flatpak build, the same smoke test, and finally the bundle and its
checksum attached to a GitHub release. Both are Linux-only.
