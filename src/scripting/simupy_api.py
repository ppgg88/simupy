"""SimuPy — write simulation blocks in Python.

Subclass :class:`Block`, declare how many ports and states you need, and
implement the handful of methods the solver calls.

    import numpy as np
    from simupy import Block

    class LowPass(Block):
        inputs = ["u"]
        outputs = ["y"]
        states = 1
        direct_feedthrough = False

        def setup(self, widths):
            self.tau = self.params.get("tau", 0.5)

        def output(self, t, u):
            return self.x

        def derivative(self, t, u):
            return (u[0] - self.x) / self.tau

The solver sets ``self.x`` (continuous state) and ``self.xd`` (discrete
state) to NumPy arrays that view the engine's own buffers before each call,
so reading them is free. Write to them only through the return values of
:meth:`derivative` and :meth:`update`.
"""

import numpy as np

__all__ = ["Block", "log", "CONTINUOUS", "INHERIT"]

# Sample-time sentinels.
CONTINUOUS = 0.0
#: Return this from ``setup`` to keep an output the width of the input.
INHERIT = -1

class Block:
    """Base class for every Python block.

    Class attributes declare the block's shape. Override the ones you need;
    every value below is the default.
    """

    #: Number of input ports, or a list of port names.
    inputs = 1
    #: Number of output ports, or a list of port names.
    outputs = 1
    #: Number of continuous states integrated by the solver.
    states = 0
    #: Number of discrete states advanced by :meth:`update`.
    discrete_states = 0
    #: 0.0 for a continuous block, or a period in seconds for a discrete one.
    sample_time = CONTINUOUS
    #: Number of zero-crossing functions returned by :meth:`zero_crossing`.
    zero_crossings = 0
    #: Set to False when :meth:`output` ignores its inputs. Doing so lets the
    #: block sit inside a feedback loop without creating an algebraic loop.
    direct_feedthrough = True

    # Populated by the engine before setup() runs.
    params = {}
    x = None
    xd = None

    # ------------------------------------------------------------------
    # Structure
    # ------------------------------------------------------------------

    def setup(self, widths):
        """Called once per run, before the simulation starts.

        ``widths`` is the list of input signal widths (0 for an unconnected
        port). Return the output width, a list of widths, or None to give
        every output the width of the widest input.

        Read parameters and precompute constants here.
        """
        return None

    def initial_state(self):
        """Initial value of ``self.x``. Defaults to zeros."""
        return None

    def initial_discrete_state(self):
        """Initial value of ``self.xd``. Defaults to zeros."""
        return None

    # ------------------------------------------------------------------
    # Runtime
    # ------------------------------------------------------------------

    def output(self, t, u):
        """Return this block's outputs at time ``t``.

        ``u`` is a list of read-only NumPy arrays, one per input port. Return
        a scalar or array for a single output, or a list of them when the
        block has several.

        This runs several times per accepted step, at trial states the solver
        may discard, so it must be free of side effects.
        """
        raise NotImplementedError(
            "%s must implement output()" % type(self).__name__)

    def derivative(self, t, u):
        """Return dx/dt as an array of length ``states``."""
        return np.zeros(self.states)

    def update(self, t, u):
        """Return the next discrete state, of length ``discrete_states``.

        Called once per sample hit, after :meth:`output`.
        """
        return self.xd

    def zero_crossing(self, t, u):
        """Return the block's zero-crossing functions, length ``zero_crossings``.

        Each entry is a signed quantity whose sign change marks a
        discontinuity in the block's output — a threshold comparison, say,
        returns ``value - threshold``. The solver brackets any sign change and
        lands the step exactly on the event instead of integrating across it,
        which is what keeps a switching model accurate.
        """
        return None

    def terminate(self):
        """Called once when the run ends. Release resources here."""

def log(*args, sep=" "):
    """Write a line to the SimuPy console."""
    print(sep.join(str(a) for a in args))

# ----------------------------------------------------------------------
# Talking to real hardware
# ----------------------------------------------------------------------

def require(module, package=None, purpose=None):
    """Import an optional dependency, or say plainly how to install it.

    A bare ``import serial`` inside a block gives whoever opens the model a
    ModuleNotFoundError and a traceback through SimuPy's own machinery. This
    gives them the pip command instead::

        serial = require("serial", "pyserial", "talking to an Arduino")

    Returns the imported module.
    """
    import importlib

    try:
        return importlib.import_module(module)
    except ImportError as error:
        package = package or module
        reason = f" ({purpose})" if purpose else ""
        raise ImportError(
            f"this block needs the '{module}' module{reason}, which is not "
            f"installed.\n"
            f"Install it with:  pip install {package}\n"
            f"SimuPy does not depend on it — only blocks that talk to this "
            f"kind of device do."
        ) from error

class HardwareBlock(Block):
    """Base for a block that owns something outside the process.

    Handles the part that is easy to get wrong: opening once per run, closing
    exactly once however the run ends, and never letting a failure to close
    mask the error that caused it.

    Subclasses implement :meth:`open_device` and :meth:`close_device` and use
    ``self.device``. Everything else is an ordinary block.

    A run holds Python's global lock, and SimuPy hands it back while a
    real-time run is waiting between steps — which is where the wall clock
    goes. A background thread doing the actual reading therefore gets time to
    run, and :meth:`output` can return the latest value it left behind instead
    of blocking the solver on a port. Blocking directly in ``output()`` works,
    but every microsecond it waits is a microsecond the simulation is late.
    """

    _device = None

    @property
    def device(self):
        """The device, opened on first use.

        Deliberately lazy. Signal widths are propagated to a fixpoint, so the
        solver calls ``setup()`` on a fresh instance several times per run and
        keeps only the last. Opening in ``setup()`` would open the port once
        per attempt — the second would fail with the device already busy, and
        the discarded instances would never close theirs.

        Touching this from :meth:`configure` puts you back in that situation,
        so query the device from ``output()`` or ``update()`` instead unless
        you genuinely need it to decide a width.
        """
        if self._device is None:
            self._device = self.open_device()
        return self._device

    def open_device(self):
        """Open and return the device. Called once per run, on first use."""
        raise NotImplementedError

    def close_device(self, device):
        """Release `device`. Called once, however the run ended."""
        try:
            device.close()
        except AttributeError:
            pass

    def setup(self, widths):
        return self.configure(widths)

    def configure(self, widths):
        """Override instead of ``setup()``. Same contract as ``Block.setup``.

        The device is not open yet — see :attr:`device`.
        """
        return None

    def terminate(self):
        device, self._device = self._device, None
        if device is None:
            return
        try:
            self.close_device(device)
        except Exception as error:  # noqa: BLE001 - reported, never raised
            # Raised here, this would replace whatever actually went wrong
            # with a complaint about the tidying up.
            log(f"warning: could not close {type(self).__name__}: {error}")

# ----------------------------------------------------------------------
# Arduino / ESP32 bridge
# ----------------------------------------------------------------------

class ArduinoBoard:
    """One shared link to a board running `firmware/simupy_bridge`.

    Lives here rather than in a block because several blocks — an analog read,
    a PWM write, a digital read — all address the same board through the same
    port, and each block's script runs in its own namespace. One connection
    per (port, baud), reference counted, is the only arrangement that lets a
    model wire up four pins without four programs fighting over one device.

    The board streams its configured inputs continuously and this class keeps
    the newest sample. A block's ``output()`` therefore reads a local value
    instead of asking the board and waiting: at 115200 baud a round trip costs
    around a millisecond, which a control loop cannot afford to spend on every
    step of every pin.
    """

    _boards = {}

    # -- lifetime -----------------------------------------------------------

    @classmethod
    def acquire(cls, port, baud=115200, rate_hz=200):
        key = (str(port), int(baud))
        board = cls._boards.get(key)
        if board is None:
            board = cls(key[0], key[1], rate_hz)
            cls._boards[key] = board
        board._users += 1
        if board._users == 1:
            # First user of a new run: forget the pin list the previous model
            # asked for, or a pin nothing reads any more would still be
            # streamed.
            board._reset_inputs()
        return board

    def release(self):
        self._users -= 1
        if self._users > 0:
            return
        self._close()
        ArduinoBoard._boards.pop((self.port, self.baud), None)

    def __init__(self, port, baud, rate_hz):
        import threading

        serial = require("serial", "pyserial", "talking to an Arduino")

        self.port = port
        self.baud = baud
        self.rate_hz = int(rate_hz)
        self._users = 0

        self.board_name = "unknown"
        self.adc_bits = 10
        self.pwm_bits = 8

        self._lock = threading.Lock()
        self._inputs = []          # [(pin, analog)] in the order sent
        self._values = {}          # (pin, analog) -> latest reading
        self._generation = 0       # the newest sample's generation
        self._pending_orders = []  # orders sent but not yet acknowledged
        self._orders = {}          # generation -> pin order, as acknowledged
        self._samples = 0

        self._link = serial.Serial(port=port, baudrate=baud, timeout=0.2,
                                   write_timeout=0.5)
        # Boards with a native USB stack reset when the port opens; the sketch
        # sends its banner once it is up, and this waits for it rather than
        # guessing a delay.
        self._running = True
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

        self._command("?")
        self._await_banner(timeout=3.0)
        self._command(f"R {self.rate_hz}")

    def _await_banner(self, timeout):
        import time

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.board_name != "unknown":
                return
            time.sleep(0.02)
            self._command("?")   # a board still resetting missed the first one
        log(f"warning: {self.port} did not identify itself; assuming "
            f"{self.adc_bits}-bit analog input")

    def _close(self):
        self._running = False
        try:
            self._command("C")   # stop the stream before the port goes
        except Exception:
            pass
        if self._reader.is_alive():
            self._reader.join(timeout=1.0)
        try:
            self._link.close()
        except Exception:
            pass

    # -- the wire -----------------------------------------------------------

    def _command(self, text):
        try:
            self._link.write((text + "\n").encode("ascii"))
        except Exception as error:
            log(f"Arduino {self.port}: {error}")

    def _read_loop(self):
        # Buffered here rather than left to readline(): pyserial returns
        # whatever it has when its timeout expires, newline or not, so a line
        # straddling a timeout arrives in two pieces. Splitting those into two
        # "lines" would corrupt a sample and, worse, desynchronise the
        # acknowledgement queue for the rest of the connection.
        pending = b""

        while self._running:
            try:
                waiting = self._link.in_waiting or 1
                chunk = self._link.read(waiting)
            except Exception:
                break
            if not chunk:
                continue

            pending += chunk
            # A line that never terminates means the far end is not speaking
            # this protocol; drop it rather than grow without bound.
            if len(pending) > 8192:
                pending = b""
                continue

            while b"\n" in pending:
                raw, pending = pending.split(b"\n", 1)
                line = raw.decode("ascii", "replace").strip()
                if not line:
                    continue

                if line.startswith("#simupy"):
                    self._parse_banner(line)
                elif line.startswith("#C"):
                    self._parse_config_ack(line)
                elif line.startswith("!"):
                    log(f"Arduino {self.port}: {line[1:]}")
                else:
                    self._parse_sample(line)

    def _parse_banner(self, line):
        fields = line.split()
        try:
            self.board_name = fields[2]
            self.adc_bits = int(fields[3])
            self.pwm_bits = int(fields[4])
        except (IndexError, ValueError):
            pass

    def _parse_config_ack(self, line):
        fields = line.split()
        if len(fields) < 2:
            return
        try:
            generation = int(fields[1])
        except ValueError:
            return
        with self._lock:
            if not self._pending_orders:
                return
            self._orders[generation] = self._pending_orders.pop(0)
            # Only the newest configuration can still produce samples; older
            # ones would otherwise accumulate for the life of the connection.
            self._orders = {g: o for g, o in self._orders.items()
                            if g >= generation}

    def _parse_sample(self, line):
        fields = line.split()
        if len(fields) < 2:
            return
        try:
            generation = int(fields[0])
            values = [int(f) for f in fields[2:]]
        except ValueError:
            return   # a partial line, or noise on a just-opened port

        with self._lock:
            order = self._orders.get(generation)
            # A sample already in flight when the pin list changed would pair
            # values with the wrong pins. Dropping it costs one sample.
            if order is None or len(order) != len(values):
                return
            for key, value in zip(order, values):
                self._values[key] = value
            self._generation = generation
            self._samples += 1

    # -- inputs -------------------------------------------------------------

    def _send_config(self, order):
        """Sends one C command and queues the order it will establish.

        The generation is not assumed — it is read back from the board's `#C`
        acknowledgement. A board only starts counting from zero if it reset
        when the port opened, which an Uno does and an ESP32 does not, and
        which nothing does on a second run against a board already powered.
        Guessing gets it wrong in exactly those cases, and every sample is
        then discarded as belonging to a configuration we do not recognise.

        Acknowledgements come back in the order the commands were sent, so a
        queue pairs them up even with several in flight.
        """
        with self._lock:
            self._pending_orders.append(list(order))

        names = " ".join(("A" if a else "D") + str(p) for p, a in order)
        self._command(("C " + names) if names else "C")

    def _reset_inputs(self):
        with self._lock:
            self._inputs = []
            self._values = {}
        self._send_config([])

    def add_input(self, pin, analog):
        """Registers a pin to be streamed. Idempotent."""
        key = (int(pin), bool(analog))
        with self._lock:
            if key in self._inputs:
                return
            self._inputs.append(key)
            order = list(self._inputs)
        self._send_config(order)

    def read(self, pin, analog):
        """The newest reading: 0..1 for an analog pin, 0 or 1 for a digital one."""
        key = (int(pin), bool(analog))
        with self._lock:
            raw = self._values.get(key)
        if raw is None:
            return 0.0
        if not analog:
            return 1.0 if raw else 0.0
        return raw / float((1 << self.adc_bits) - 1)

    @property
    def sample_count(self):
        """Samples accepted since the link opened. Zero means nothing arrived."""
        with self._lock:
            return self._samples

    # -- outputs ------------------------------------------------------------

    def set_mode(self, pin, mode):
        self._command(f"M D{int(pin)} {mode}")

    def write_pwm(self, pin, duty):
        """`duty` is 0..1; the board scales it to its own PWM width."""
        duty = 0.0 if duty < 0.0 else (1.0 if duty > 1.0 else duty)
        self._command(f"P D{int(pin)} {duty:.4f}")

    def write_digital(self, pin, level):
        self._command(f"W D{int(pin)} {1 if level else 0}")

class ArduinoBlock(HardwareBlock):
    """Base for the blocks in the Arduino library.

    Turns ``self.device`` into a shared :class:`ArduinoBoard` rather than a
    private port, so every block pointed at the same device cooperates instead
    of competing for it.
    """

    inputs = 0
    outputs = 0
    direct_feedthrough = False

    def open_device(self):
        return ArduinoBoard.acquire(self.params["port"],
                                    int(self.params.get("baud", 115200)),
                                    int(self.params.get("rate", 200)))

    def close_device(self, device):
        device.release()

