# SimuPy

A block-diagram simulator in the spirit of Simulink, where custom blocks are
written in Python.

The engine, the editor and the solvers are C++. When a diagram needs behaviour
that no built-in block provides, you drop in a **Python** block and write it as
a small class — no plugin to compile, no restart. The script is recompiled at
the start of every run, so editing and pressing Run is the whole loop.

![the editor](docs/_static/editor.png)

---

## Quick start

```sh
sudo apt install cmake g++ qt6-base-dev qt6-charts-dev qt6-svg-dev \
                 libeigen3-dev nlohmann-json3-dev python3-dev \
                 python3-pybind11 python3-numpy

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/bin/simupy examples/pid_control_loop.spy
```

Point at the canvas, start typing, pick a block. Press **F5**.

## What it does

- Variable- and fixed-step solvers, with **event location** so a switching
  model lands exactly on its discontinuities.
- **Algebraic loops are solved**, not refused, by Newton iteration on the torn
  signals.
- **Subsystems** with mask parameters, saved into shareable `.spylib`
  libraries.
- **Real-time pacing** and **interactive controls**, so a model can be watched
  and driven while it runs.
- **Hardware in the loop** over UDP, serial, ROS 2, or an Arduino running the
  bundled firmware.

## Documentation

Full documentation lives in [`docs/`](docs/) as reStructuredText:

```sh
pip install -r docs/requirements.txt
sphinx-build -b html docs docs/_build/html
```

| Page | |
|---|---|
| [installation](docs/installation.rst) | Building, packaging, CI |
| [first-model](docs/first-model.rst) | Your first model, end to end |
| [guide/](docs/guide/) | Editor, subsystems, signals, simulation, controls, libraries, hardware |
| [reference/](docs/reference/) | Blocks, the Python block API, file formats, command line |
| [internals/](docs/internals/) | Architecture, and how the engine works |
| [limitations](docs/limitations.rst) | What to know before relying on it |

## Testing

```sh
ctest --test-dir build --output-on-failure
```

`simupy-tests` covers the engine; `simupy-gui-tests` drives the real window
offscreen with synthesised key and mouse events.

## Layout

```
src/model/       the data a diagram is made of, and flattening
src/blocks/      the built-in block set
src/engine/      scheduling, integration, logging, pacing
src/scripting/   the embedded interpreter and Python blocks
src/io/          .spy and .spylib, custom block libraries
src/app/         the command-line runner and the desktop application
firmware/        the Arduino / ESP32 sketch
libraries/       the shipped hardware block library
tools/           a board simulator, for working without hardware
```

Seven static libraries with an acyclic dependency graph: the model and engine
build without an interpreter, and all of it without Qt. See
[docs/internals/architecture.rst](docs/internals/architecture.rst).

## Examples

| File | What it shows |
|---|---|
| `mass_spring_damper.spy` | A second-order plant against its driving step |
| `pid_control_loop.spy` | Unity feedback, PID with anti-windup, mirrored sensor |
| `signal_playground.spy` | The source and math blocks, side by side |
| `python_lorenz.spy` | The Lorenz attractor as one Python block |
| `python_discrete_controller.spy` | A 50 Hz Python PI controller driving a continuous C++ plant |
| `subsystem_controller.spy` | The same loop with the controller folded into a subsystem |
| `live_controls.spy` | A real-time loop steered from the Controls dock while it runs |
