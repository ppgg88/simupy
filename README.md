# SimuPy

A block-diagram simulator in the spirit of Simulink, where custom blocks are
written in Python.

The engine, the editor and the solvers are C++. When a diagram needs behaviour
that no built-in block provides, you drop in a **Python** block and write it as
a small class — no plugin to compile, no restart. The script is recompiled at
the start of every run, so editing and pressing Run is the whole loop.

![the editor](docs/editor.png)

---

## Building

Requirements — all available as distribution packages on Ubuntu 24.04:

| Dependency | Minimum | Ubuntu package |
|---|---|---|
| CMake | 3.21 | `cmake` |
| C++ compiler | C++17 | `g++` |
| Qt | 6.2 (Widgets, Charts) | `qt6-base-dev qt6-charts-dev` |
| Eigen | 3.3 | `libeigen3-dev` |
| nlohmann/json | 3.0 | `nlohmann-json3-dev` |
| Python | 3.8 + headers | `python3-dev` |
| pybind11 | 2.6 | `python3-pybind11` |
| NumPy | any | `python3-numpy` |
| Qt SVG *(optional)* | 6.2 | `qt6-svg-dev` |

Qt SVG is only used for vector icons on custom blocks. Without it the build
succeeds and PNG/JPEG icons still work; CMake reports which of the two you got.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/bin/simupy examples/pid_control_loop.spy
```

`-DSIMUPY_BUILD_GUI=OFF` builds only the engine, the command-line runner and
the tests — useful on a headless machine.

### Checking the build

```sh
ctest --test-dir build --output-on-failure
```

`simupy-tests` covers the engine: solver order of accuracy against closed-form
solutions, event location, algebraic-loop solving, stiff integration,
signal-width propagation, discrete sample timing, subsystem flattening,
wireless-link resolution and scoping, mask parameter binding, library
round-tripping, copy and paste, real-time pacing, unbounded runs and the log
decimation that makes them possible, interactive controls, the hardware block
lifecycle over a real UDP socket, the Arduino bridge against a board simulated
on a pseudo-terminal, and the Python bridge. `simupy-gui-tests` drives the real window offscreen — including synthesised key and mouse events
— to check hierarchy navigation, the editing operations, signal naming,
type-to-insert, driving a slider across the thread boundary into a live run,
and that the canvas shortcuts do not follow the focus into a text field.

---

## Using it

### The editor

- **Just start typing** over the canvas to search every block type and drop
  the one you pick at the cursor — `ste`↵ for a Step, `tf`↵ for a Transfer
  Function. Matching runs on the name, the category and the description, and
  falls back to a subsequence, so an abbreviation usually finds it. **Ctrl+Space**
  does the same from the menu, **Esc** abandons.
- **Drag** a block from the library onto the canvas, or double-click it in the
  library to drop it in the middle of the view.
- **Wire** by dragging from an output port to an input port — either direction
  works. An input accepts one wire; a new one replaces the old.
- **Double-click** a block to edit it: a Scope opens its plot, a Python block
  opens the code editor, a Subsystem opens its contents, anything else focuses
  the property panel.
- A plot opens as its own window. **On top** pins it above the main window so
  it stays visible while you work on the diagram; **Dock** folds it into a tab
  beside the Console instead. Each plot decides for itself, and either can also
  be dragged into place by its title bar.
- **Ctrl+Down** enters the selected subsystem, **Ctrl+Up** leaves it. The
  breadcrumb above the canvas shows where you are and jumps back a level.
- **Ctrl+M** mirrors the selection, which is how you draw a feedback path
  right-to-left.
- **F2**, or a double-click on a wire, names the signal it carries.
- **Ctrl+C / Ctrl+X / Ctrl+V** copy, cut and paste the selection; **Ctrl+D**
  duplicates it in place. A paste lands at the cursor, and the copies come out
  selected so they can be dragged straight away.
- **Shift+drag** or the middle button pans; **Ctrl+wheel** zooms.
- **F5** runs, **Shift+F5** stops — the toolbar carries the same two, with
  the green ▶ and the red ■, and greys out whichever one does not apply.

Wires show their signal width once a run has compiled the model, so a vector
signal is visible at a glance.

### Subsystems

Drop a **Ports → Subsystem** block, open it, and build inside. An **Inport**
becomes an input port on the enclosing block and an **Outport** an output; the
number on each says which port it is, so ports keep their wiring when you add
or reorder them.

Subsystems are purely organisational. They are expanded before the model is
compiled, so a block inside one integrates, samples and participates in
feedback exactly as it would at the top level — nesting costs nothing at run
time. Logged signals are named by their path (`PI Controller/Integral`), which
keeps two copies of the same subsystem apart.

### Running in real time

By default a run finishes as fast as the machine allows — eight seconds of
model time in seventy milliseconds. That is what you want when you are after a
final number, and the wrong thing entirely when you are watching a scope,
demonstrating a controller, or driving something outside the process.

The **clock button** on the toolbar paces the run against the wall clock.
*Simulation → Settings → Pacing* sets the speed: `1x` is real time, `0.25x`
quarter speed for something that happens too fast to see, `10x` when real time
is more patience than the model deserves. The setting is saved with the model.

**Pacing changes the speed, not the answer.** The solver takes exactly the same
steps and produces bit-identical results; the run simply waits between them.

Deadlines are absolute rather than incremental — each is computed from the
start of the run, not from the previous one — so a step that overruns is
absorbed instead of pushing every later step further out. A run that cannot
compute fast enough falls behind rather than skipping work, and the summary
says so:

```
Finished at t = 8 s — 235 steps … — real time held, worst lag 0.4 ms
Finished at t = 8 s — 235 steps … — COULD NOT KEEP UP: worst lag 1.8 s,
                                    resynced 2 times
```

Past a second of lag the pacer restarts its clock rather than spending the rest
of the run trying to catch up — which is also what absorbs a laptop suspending
mid-run. Every restart is counted and reported, so "real time held" means it
actually was.

### Running with no end

The **∞ button** beside the stop time drops the end time entirely: the run
keeps going until Stop is pressed. It pairs naturally with real time — a
console you leave open and steer — and the two toggles sit side by side for
that reason. An unbounded run at full speed is legal but simply burns a core.

**Memory stays bounded.** The log decimates itself as it fills, halving in
place, so the samples thin out but the trace still spans the whole run. A
nine-second unbounded run of `mass_spring_damper.spy` at full speed reaches
1.25 million simulated seconds and holds flat at 40 MB.

The stop time is kept rather than cleared, so switching ∞ back off returns the
model to the end time it had.

From the shell:

```sh
simupy-cli run model.spy --realtime        # 1x
simupy-cli run model.spy --realtime 0.25   # quarter speed
simupy-cli run model.spy --unbounded       # until Ctrl+C
```

Ctrl+C on an unbounded run is a clean stop, not a kill: the run ends at the
next major step, and the summary and the `--csv` file are still written.

### Driving a model while it runs

A real-time run you can only watch is a film. The **Controls** category adds
three blocks you drive from the dock of the same name, while the simulation is
in flight:

| Block | What it does |
|---|---|
| `Slider` | a value dragged between two bounds — a setpoint, a gain, a load |
| `Toggle` | two values, latched — an enable, a mode, a fault to inject |
| `PushButton` | its on value only while held — a disturbance, a reset pulse |

Drop one, wire its output like any other source, and it appears in the
Controls dock labelled by its path through the hierarchy. A control three
subsystems deep is still something you want to reach without navigating to it.

**Or drive it on the block itself.** Click the switch to flip it, hold the
button face to press it, drag the slider's track to move it — the cursor
changes over any part of a block that can be driven. The zone is deliberately
smaller than the block, so dragging anywhere else still moves the block around
the canvas.

Both routes write to the same value, so the dock and the diagram always agree.

While a run is in flight the diagram is **locked, not disabled**: it still
pans, zooms, selects and takes control input, but nothing can be moved, wired,
deleted or pasted. The library and the property panel are disabled outright,
because editing a model mid-run is not a thing — driving one is.

A control set *before* pressing Run starts the run in that state. Flipping a
switch and then starting is how you set an initial condition, so the run seeds
itself from the parameters only when nobody has touched the control.

`examples/live_controls.spy` is a loop built to be steered: move the setpoint
and watch it track, turn off the integral action and watch the offset appear,
hold the disturbance button and watch it reject.

**How it crosses the thread boundary.** The solver runs on a worker thread and
the interface on the main one, so a control's value lives in a `std::atomic`
on the block. That is the only thing the two threads share: parameters are
read once at setup and never again, so moving a slider cannot race with
anything the solver is doing.

A change lands at the **next evaluation**. With a variable-step solver that
means it takes effect somewhere inside the step in progress rather than at a
declared instant — the solver has no way to know a hand was about to move. For
a slider that is invisible. For a toggle or a button it is a genuine
discontinuity crossed blind, so if the exact switching instant matters, cap
the maximum step (Settings → Max step) to bound how far off it can be.

Whatever the controls are left at when the run ends becomes the model's own
value, so a setpoint you tuned mid-run is still there when you save.

### Talking to real hardware

Nothing special is needed: a Python block can `import serial` and drive an
Arduino, or `import rclpy` and publish to ROS 2. What the project adds is the
handful of things that make it work rather than nearly work.

**The optional dependency stays optional.** `require()` imports it and, when
it is missing, says which package and why instead of throwing a traceback
through SimuPy's own machinery:

```python
serial = require("serial", "pyserial", "talking to an Arduino")
# ImportError: this block needs the 'serial' module (talking to an Arduino),
# which is not installed.
# Install it with:  pip install pyserial
```

**`HardwareBlock` owns the lifecycle.** Subclass it, implement `open_device()`
and `close_device()`, use `self.device`. The device opens on first use and
closes exactly once however the run ended — and a failure to close is reported
rather than allowed to mask whatever actually went wrong.

Opening lazily is not an accident. Signal widths propagate to a fixpoint, so
the solver calls `setup()` on a fresh instance several times per run and keeps
only the last. A port opened in `setup()` would be opened once per attempt —
the second failing with the device busy — and the discarded instances would
never close theirs.

**Blocking reads belong on a thread.** A run holds Python's global lock from
start to finish, and SimuPy hands it back while a real-time run waits between
steps — which is where nearly all the wall clock goes. A background thread
reading a serial port therefore gets the time it needs, and `output()` returns
the value it last left behind instead of making the simulation late by however
long the device took to answer. `SerialReceive` in the shipped library is
written this way and is worth reading before writing your own.

#### Arduino and ESP32

Flash `firmware/simupy_bridge/simupy_bridge.ino` once and the board's pins
become blocks. Nothing on the board is model-specific: the model says at run
time which pins to read and write.

| Block | Does |
|---|---|
| `ArduinoAnalogRead` | reads an analog pin, 0..1 scaled by *Full scale* |
| `ArduinoDigitalRead` | reads a digital pin, with optional pull-up |
| `ArduinoAnalogWrite` | drives a PWM pin from a 0..1 duty |
| `ArduinoDigitalWrite` | drives a pin high above a threshold, with a declared zero crossing so it switches at the instant the model says |

Set every block's **Port** to the same device and they share one connection —
`ArduinoBoard` is reference counted per port, because four blocks opening four
handles on one `/dev/ttyUSB0` would simply fight.

**The board streams; the host never asks.** It sends the configured input pins
continuously at a rate the model sets, and a block's `output()` reads the
newest sample from a local cache. A request-response per pin per step would
cost about a millisecond of round trip at 115200 baud, which a control loop
cannot spend.

Duty cycles cross the wire as 0..1 and the board scales them to its own PWM
width, so the same model drives an Uno's 8 bits and an ESP32's 12 without
knowing which it is talking to. Analog readings come back as raw counts and
are normalised using the resolution the board reported when it identified
itself.

Boards: AVR (Uno, Nano, Mega, Leonardo, Micro), SAMD, RP2040, ESP8266, and
ESP32 on both the 2.x and 3.x cores — the 2.x core has no `analogWrite`, so
the sketch falls back to LEDC. Everything board-specific is in one HAL section
at the top of the sketch; the protocol below it is identical everywhere.

#### Trying it without a board

```sh
python3 tools/fake_arduino.py --mode echo
/dev/pts/7
```

That is a board, in Python, speaking the same protocol on a pseudo-terminal.
Point the blocks' **Port** at the path it prints and the model runs exactly as
it would over USB — useful for building a model before the hardware arrives,
and what the test suite uses to cover the host side without any.

`--mode echo` makes analog input *n* read back whatever was last written to
PWM pin *n*, which closes a loop through the "board"; the default reads a slow
sine on every input so a plot shows something recognisable.

#### The shipped library

`libraries/hardware.spylib` — install it with *Library → Manage Libraries →
Install*, or point `SIMUPY_LIBRARY_PATH` at the `libraries/` directory.

| Block | Needs | Notes |
|---|---|---|
| `ArduinoAnalogRead` / `Write`, `ArduinoDigitalRead` / `Write` | `pyserial` | a board running `firmware/simupy_bridge`; see above |
| `UdpSend` / `UdpReceive` | nothing | float64s over a socket; the receiver is non-blocking and holds its last value |
| `SerialSend` / `SerialReceive` | `pyserial` | text lines by default, which is what `Serial.parseFloat()` expects |
| `Ros2Publish` | a sourced ROS 2 | `std_msgs/Float64MultiArray`; a starting point to change the message type on |

The UDP pair depends on nothing at all, which makes it the quickest way to
check the path works — and it is what the test suite exercises, through a real
socket, so the send, the bind, the non-blocking drain and the lifecycle are all
genuinely covered without any hardware present.

**Pair it with real time.** A model driving hardware flat out will send a
year of setpoints in a second. The clock button is what makes the two agree.

### Copying blocks

Select one block or many and **Ctrl+C**, then **Ctrl+V** to paste at the
cursor. **Ctrl+D** duplicates the selection alongside itself without touching
the clipboard, which is the quicker gesture when you are laying out a repeated
structure.

What travels with a copy: the blocks, their parameters, their signal names and
mask bindings, everything nested inside a Subsystem, and the wires running
between two selected blocks. A wire with one end outside the selection is
dropped — it would have nothing to reconnect to. Pasted blocks get fresh ids
and, where a name is taken, a fresh name, so pasting back into the diagram you
copied from is as safe as pasting into another one.

A selection goes on the clipboard as JSON, under a private MIME type **and** as
plain text. That means a copy can be pasted into a second running copy of
SimuPy, kept in a scratch file, or read in a diff — the same format the `.spy`
files use, minus the solver settings.

### Naming a signal

Double-click a wire, or select it and press **F2**, to give the signal a name.
It labels every branch of that signal on the canvas, and follows it into the
scope legend and the CSV header — so a plot says `tracking error` instead of
`Scope : in`, which is what the default gives you.

The name belongs to the **output port that produces the signal**, not to the
wire you happened to click. Every wire leaving one port carries the same
thing, so naming one branch names them all; naming them separately would let
one signal answer to two names. Clearing the name puts the default back.

### Wireless links

A signal that has to cross the diagram — a measurement fed back to three
places, a mode flag read everywhere — costs more in wire than it is worth.
Drop a **Routing → Goto** on the source and a **Routing → From** wherever you
need it, give them the same tag, and the signal arrives without a line.

Wireless links cost nothing at run time. Like Inport and Outport, they are
splice points: the flattener joins whatever drives the Goto straight to
whatever the From feeds, and neither block reaches the compiled model. A loop
closed through a tag is found and solved exactly like a wired one, and a
vector signal passes through unchanged.

A Goto's **scope** decides who can read it:

| Scope | Reachable from |
|---|---|
| `local` *(default)* | the diagram that contains it, and nothing else |
| `global` | anywhere in the model, across subsystem boundaries |

A From matches a local Goto in its own diagram first, then any global one.
That order is what lets two copies of the same subsystem both use tag `err`
without stealing each other's signal — which matters as soon as a wireless
link lives inside a library block. Keep tags local unless you mean otherwise.

Ambiguity is refused rather than guessed at. Two Gotos with one tag in the
same diagram, two global Gotos with one tag, a From whose tag nothing carries,
or a chain of links that closes on itself: each is reported by name before the
run starts.

### Saving a block for later

A subsystem you would rather not rebuild, or a Python block you want in every
model, becomes a palette block: select it and press **Ctrl+Shift+B**
(*Library → Save Selection as Block*). Give it a name, a category, an icon,
and the parameters it should expose. It appears in the palette immediately and
stays there across restarts.

**Parameters are what make a saved subsystem worth saving.** A block that
hard-codes its gains is a copy; one that exposes them is a component. Declare
them in the dialog's *Parameters* tab, then open the block and bind each inner
parameter to one with the **ƒx** button beside it. The binding is a Python
expression evaluated when the model is flattened, so all of these work:

```
kp                          a parameter straight through
[kp]                        a vector-valued Gain
[1, 2*zeta*wn, wn**2]       a denominator built from two parameters
np.diag(q)                  anything NumPy can produce
```

The names in scope are the block's own parameters, and nothing else. Bindings
nest: a custom block placed inside another custom block can pass a parameter
down. A plain `Ports → Subsystem` declares no parameters, so no ƒx buttons
appear inside it.

Python blocks get the same treatment with less work — a declared parameter
arrives as `self.params["name"]`, so a script that already reads `self.params`
needs no change. Mask values override the free-form **Parameters** field,
which becomes the place to put defaults. The names `code`, `className` and
`parameters` are the block's own and cannot be used for a mask parameter.

### Sharing them

Blocks live in **libraries**, and a library is one `.spylib` file: metadata,
every saved diagram, every Python source, and the icons base64-encoded inline.
Nothing points outside it, so sharing a library is sending the file. The
recipient drops it in `~/.local/share/simupy/libraries/`, or uses
*Library → Manage Libraries → Install*.

`SIMUPY_LIBRARY_PATH` takes a colon-separated list of extra directories, which
is how a library is kept in a project repository rather than in a home
directory. Directories are scanned in order and the user folder is scanned
last, so a local copy wins over one installed system-wide.

**A model file stays self-contained.** Saving writes each custom block's full
contents and Python source into the `.spy`, and notes which library it came
from. A model that reaches a machine without that library still opens and
still simulates exactly the same numbers — it loses the icon, the parameter
labels and the palette entry, and the console says so. Installing the library
afterwards silently restores all three, because the block remembers the type
name it wants even while standing in as a plain subsystem.

Editing a library block does not touch models already using it: they carry
their own copy. Re-drop the block to pick up a new version.

### From the shell

```sh
simupy-cli run model.spy --stop 20 --csv out.csv
simupy-cli run model.spy --realtime 2          # paced at 2x real time
simupy-cli info model.spy      # blocks, wires, layout
simupy-cli blocks              # every block type and its parameters
simupy-cli libraries           # installed libraries and where they came from
```

The GUI takes `--run` to simulate a model as soon as it opens, and `--light`
for the light theme.

---

## Writing a Python block

Drop a **Python → PythonFunction** block on the canvas and double-click it.
Subclass `simupy.Block`, declare the shape of the block with class attributes,
and implement the methods the solver calls.

```python
import numpy as np
from simupy import Block


class LowPass(Block):
    inputs = ["u"]              # a count, or a list of port names
    outputs = ["y"]
    states = 1                  # continuous states, integrated by the solver
    direct_feedthrough = False  # output() ignores u, so no algebraic loop

    def setup(self, widths):
        # widths[i] is the resolved width of input i (0 if unconnected).
        # Return the output width, a list of widths, or None to inherit.
        self.tau = self.params["tau"]

    def initial_state(self):
        return [0.0]

    def output(self, t, u):
        return self.x

    def derivative(self, t, u):
        return (u[0] - self.x) / self.tau
```

`self.x` (continuous state) and `self.xd` (discrete state) are NumPy arrays
that **view the engine's own buffers** — reading them copies nothing. They are
read-only; write state by returning it from `derivative()` or `update()`.

`u` is always a list of arrays, one per input port. Return a scalar or array
for a single output, or a list of them for several.

`self.params` is a dict built from the block's **Parameters** field, which is
plain Python (`tau = 0.5`, `gains = np.array([1, 2, 3])`), so the same script
can be reused with different values.

### Discrete blocks

Set `sample_time` to a period in seconds and implement `update()`. The block is
then evaluated only on its own sample grid; the solver still integrates the
continuous parts of the model in between, and steps exactly onto each sample
instant.

```python
class Counter(Block):
    inputs = 0
    outputs = ["n"]
    discrete_states = 1
    sample_time = 0.25
    direct_feedthrough = False

    def output(self, t, u):
        return self.xd

    def update(self, t, u):
        return self.xd + 1.0
```

### Discontinuous blocks

If a block switches — a threshold, a limit, a sign change — declare where, and
the solver will land its step exactly on the event instead of integrating
across it:

```python
class Relay(Block):
    inputs = ["u"]
    outputs = ["y"]
    zero_crossings = 1

    def output(self, t, u):
        return 1.0 if u[0][0] > 0.0 else -1.0

    def zero_crossing(self, t, u):
        return [u[0][0]]        # sign changes exactly where the output jumps
```

Without this the output still switches, but somewhere inside a step rather
than at a definite time, and the error controller will fight it.

### Rules worth knowing

- **`output()` must have no side effects.** A variable-step solver evaluates it
  several times per accepted step, at trial states it may throw away. Put state
  changes in `update()`.
- **Set `direct_feedthrough = False` whenever `output()` ignores its inputs.**
  That is what lets the block sit inside a feedback loop without creating an
  algebraic loop.
- Exceptions stop the run and are reported with the full Python traceback,
  naming the block and the method that failed.
- `print()` goes to the Console dock.
- The **Init script** (Simulation → Settings) runs once before each run; use it
  for constants and helpers shared by several blocks.

---

## What's in the box

| Category | Blocks |
|---|---|
| Sources | Constant, Step, Ramp, Sine, Clock, Pulse, Chirp, RandomNumber |
| Math | Gain, Sum, Product, Abs, Sign, MathFunction, Trigonometry, Saturation, DeadZone, MinMax, VectorReduce |
| Logic | Relational, Logic |
| Continuous | Integrator, Derivative, TransferFcn, StateSpace, PID |
| Discrete | UnitDelay, ZeroOrderHold, DiscreteIntegrator, DiscreteTransferFcn, IntegerDelay |
| Routing | Mux, Demux, Selector, Switch, ManualSwitch, Goto, From |
| Sinks | Scope, Display, Terminator, ToFile |
| Controls | Slider, Toggle, PushButton |
| Hardware *(library)* | ArduinoAnalogRead, ArduinoAnalogWrite, ArduinoDigitalRead, ArduinoDigitalWrite, UdpSend, UdpReceive, SerialSend, SerialReceive, Ros2Publish |
| Ports | Subsystem, Inport, Outport |
| Python | PythonFunction |

Plus whatever your installed libraries add — see *Saving a block for later*.

Solvers:

| Method | Order | Notes |
|---|---|---|
| Dormand-Prince 4(5) | 5 | Adaptive step with error control. The default. |
| SDIRK2 | 2 | Implicit and L-stable, for stiff models. Newton per stage with a numerically differenced Jacobian. |
| Runge-Kutta 4 | 4 | Fixed step. |
| Heun | 2 | Fixed step. |
| Euler | 1 | Fixed step. |
| Discrete | — | For models with no continuous states. |

Stiffness is what decides between the first two. On a plant with poles at −1
and −10⁵ at a relative tolerance of 1e-6, SDIRK2 finishes in 3 100 derivative
evaluations where Dormand-Prince needs 373 000 — the explicit method is held
back by stability, not accuracy. At tight tolerances on a well-conditioned
model the ranking reverses, because a second-order method needs small steps
whatever its stability region.

---

## How it fits together

Seven static libraries, one per layer, each depending only on the ones below:

```
simupy-model      Types Block BlockRegistry Model FlatModel      Eigen only
  ├── simupy-blocks     the built-in block set
  ├── simupy-engine     Scheduler Solver Simulator SignalLog Pacer
  ├── simupy-scripting  the embedded interpreter and Python blocks   + pybind11
  └── simupy-io         .spy and .spylib, custom block libraries     + nlohmann
        └── simupy-gui  style/ canvas/ panels/ dialogs/ + shell      + Qt
              simupy, simupy-cli
```

The model layer knows nothing about Python or about files, so the engine can
be built and tested without an interpreter. Masked parameters are Python
expressions, which would otherwise invert that: the evaluator is injected as a
`std::function` and supplied by the application, not reached for from inside
the flattener.

`simupy-io` sits above blocks and scripting because a library file instantiates
exactly those two kinds of block — a saved subsystem or a saved Python class.

Data flows through it like this:

```
Model            blocks, wires, layout, solver settings — pure data,
  │              nested one level per subsystem
  ▼
FlatModel        every subsystem expanded, Inport/Outport wiring spliced
  │              through, Goto/From spliced too, blocks addressed by index
  ▼
Scheduler        propagates signal widths to a fixpoint, assigns state and
  │              zero-crossing slices, then decomposes the direct-feedthrough
  │              graph into strongly connected components: singletons become
  │              plain steps, larger ones become algebraic loops with their
  │              closing edges torn
  ▼
CompiledModel    execution plan + wiring resolved to signal indices
  │
Simulator        owns every buffer; one accepted solver step is one major
  │              step and one logged sample
  ├── OdeSolver          Dormand-Prince 5(4) / SDIRK2 / RK4 / Heun / Euler
  ├── loop solver        Newton on the torn signals, numerical Jacobian
  ├── event location     bisection onto each zero crossing
  ├── RealTimePacer      absolute deadlines against the wall clock
  └── Block::compute*    native C++ blocks, or PythonBlock
                              │
                         PythonEngine — one embedded interpreter, GIL taken
                         once per run and handed back while pacing waits
```

A few decisions worth calling out:

**Subsystems are structure, not behaviour.** They are expanded before
anything else runs, so the solver, the scheduler and the loop solver only ever
see one flat diagram. Nothing below the flattening step knows hierarchy
exists.

**A custom block is a registry entry, not a new kind of block.** Loading a
library adds a `BlockType` whose factory stamps out a Subsystem pre-filled
with the saved diagram, or a Python block pre-filled with the saved source.
Everything downstream — the palette, the property panel, the serializer, the
scheduler — sees an ordinary block type and needs no idea that libraries
exist. Masks resolve in the same pass that flattens: each block's expressions
are evaluated against the parameters of the subsystem enclosing it, top-down,
so a subsystem's own freshly resolved values become the scope for its
contents and a parameter can be threaded down through nested custom blocks.
By the time the scheduler runs, every parameter is a plain value.

**Width propagation runs to a fixpoint** rather than in one topological pass.
A block sees an unresolved input as unconnected, sizes its outputs
provisionally, and is revisited until nothing changes — which settles even
when the width is determined through a feedback loop.

**Algebraic loops are solved, not refused.** Tarjan's algorithm finds each
cycle, the edges that close it are torn, and Newton iterates until
re-evaluating the group reproduces the torn values. A loop with no unique
solution — unity positive feedback around a gain of one — is reported by
name, because there the matrix J − I really is singular rather than merely
awkward.

**Events are located, not stepped over.** A block with a kink declares
zero-crossing functions; when one changes sign across an accepted step, the
step is bisected until it lands on the crossing. Two details make it work: the
trial steps are forced through the error controller, which would otherwise
refuse exactly the trials that straddle the discontinuity; and the state kept
is the one from *before* the event, since a Runge-Kutta step ending on the
crossing evaluates its last stages on the far side. Each crossing is latched
and disarmed once handled, so a state that comes to rest on its own switching
surface cannot retrigger forever.

**Wires avoid blocks, and each other.** The route is an A* search on a sparse
grid built from the endpoints and the obstacle edges. Blocks are hard
obstacles, including their name and port arrows — a wire drawn across a label
is as unreadable as one drawn across a block. Other wires are costs rather
than walls: sharing a lane is charged per unit of shared length, because two
wires on top of each other cannot be told apart, while a right-angle crossing
costs a small flat amount, below the price of a bend, so the router prefers a
clean crossing to an extra corner taken to dodge it. Wires carrying the same
signal are exempt from both, which is what lets a fan-out read as one
branching line.

**A signal's name lives on the port that produces it.** Not on the wire, and
not on the sink that logs it: a fan-out is one signal drawn several times, and
a wireless link is the same signal with no wire at all. Naming the producer is
the only place that stays true through both, which is why the label survives a
branch, a Goto/From hop, and a subsystem boundary without any special case.

**Logging is tied to accepted solver steps.** That is what makes a
variable-step plot dense through a fast transient and sparse through a quiet
stretch. The log decimates itself once it hits the sample cap, halving in
place, so a long run stays within bounded memory without losing its shape.

**The GIL is taken once for the whole run.** Acquiring it per block evaluation
would cost more than most blocks do.

---

## Limits

Worth knowing before you rely on it:

- **Python blocks are serialised by the GIL.** Several of them in one model run
  one at a time. A single in-process interpreter also means a segfault in a
  native extension takes the application with it. This is inherent to the
  embedded design and would need an out-of-process worker to change.
- **Algebraic loops are solved numerically, so they cost.** Each loop runs a
  Newton solve on every output evaluation, and the Jacobian is rebuilt by
  finite differences each iteration. A loop is still worth breaking with a
  Unit Delay when you can.
- **Zero-crossing detection needs the block to declare it.** The built-in
  discontinuous blocks do, and a Python block can via `zero_crossings` and
  `zero_crossing()`. A block that switches without declaring anything is
  still crossed blindly.
- **Two events inside one step are found one at a time.** The step lands on
  the earliest crossing and the next step finds the rest, which is right but
  can be slow if a model chatters between two surfaces.
- **Subsystems are virtual.** They organise a diagram but do not create a
  scope of their own: there is no enabled/triggered execution, and no
  atomic-subsystem semantics. There is also no "group the selection into a
  subsystem" command yet — build one by dropping a Subsystem block and
  filling it in, which is also how you start a block worth saving.
- **A mask expression is evaluated at flatten time, and overwrites the typed
  value.** Opening a library block and editing a bound parameter by hand does
  nothing: the next run computes it again from the mask. The property panel
  shows one or the other, never both, so the trap is visible rather than
  silent — but it is worth knowing before you go looking for the value you
  typed.
- **Library blocks are copies, not links.** Dropping one stamps out the saved
  diagram; editing the library afterwards leaves existing instances alone.
  That is what keeps a `.spy` self-contained and reproducible, and it is the
  opposite of what a Simulink library link does. Re-drop the block to take a
  new version.
- **A wireless link is invisible on the canvas.** That is the point, and it is
  also the cost: a diagram wired entirely through tags is harder to read than
  one with lines, because nothing shows you where a signal goes. Use them for
  the few signals that genuinely cross the whole diagram, not as a default.
  There is no "find every From reading this tag" command yet.
- **Mask expressions need Python.** They are evaluated by the embedded
  interpreter, so a masked block in a build where Python failed to start
  reports the failure rather than falling back to its typed values.
- **SDIRK2 is only second order.** It earns its keep on stiff models; on a
  well-conditioned one at a tight tolerance the explicit method is cheaper.
  There is no automatic stiffness detection, so the choice is yours.
- **Wire routing is greedy.** Wires are laid one after another, each avoiding
  the ones already placed, so the result is good but not globally optimal — a
  different order could occasionally do better. There are also no
  user-editable waypoints: the route is entirely the router's choice.
- **`Derivative`** is a plain finite difference between major steps: first
  order, and it amplifies noise. Prefer a filtered derivative
  (`s/(tau*s + 1)` as a Transfer Function) where accuracy matters.

---

## The file format

`.spy` files are plain JSON — readable in a diff, editable by hand, and
sensible to keep under version control:

```json
{
  "format": "simupy-model",
  "version": 1,
  "name": "pid control loop",
  "solver": { "method": "rk45", "stopTime": 8.0, "relTol": 1e-6 },
  "blocks": [
    { "id": "plant", "type": "TransferFcn", "name": "Plant",
      "geometry": { "x": 20, "y": 0, "width": 130, "height": 60 },
      "params": { "numerator": [2.0], "denominator": [1.0, 3.0, 2.0] },
      "signals": { "0": "plant output" } }
  ],
  "connections": [
    { "id": "wire_1",
      "source": { "block": "pid", "port": 0 },
      "target": { "block": "plant", "port": 0 } }
  ]
}
```

`examples/make_examples.py` generates the sample models this way and is the
easiest place to look when scripting model generation.

A block library is the same idea in a `.spylib` file — one document holding
every saved block, each with its mask, its icon and either a diagram or a
Python source:

```json
{
  "format": "simupy-library",
  "version": 1,
  "name": "Control Toolbox",
  "revision": 3,
  "blocks": [
    { "name": "PIAntiWindup", "kind": "subsystem", "category": "Control",
      "icon": { "kind": "text", "text": "PI" },
      "params": [
        { "name": "kp", "label": "Proportional gain",
          "kind": "real", "default": 1.0 }
      ],
      "contents": {
        "blocks": [
          { "id": "gain", "type": "Gain", "name": "Kp",
            "params": { "gain": [1.0] },
            "expressions": { "gain": "[kp]" } }
        ],
        "connections": []
      } }
  ]
}
```

`expressions` is the mask binding: `params` holds the last resolved value so
the file stays readable, and `expressions` holds what actually decides it.

---

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
