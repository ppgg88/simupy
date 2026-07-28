#!/usr/bin/env python3
"""Writes the example .spy models.

The format is plain JSON, so the models are easier to author here — with
loops and shared layout helpers — than by hand.
"""

import json
import pathlib

HERE = pathlib.Path(__file__).parent

class Builder:
    """Small helper that lays blocks out on a grid and wires them up."""

    def __init__(self, name, **solver):
        self.name = name
        self.blocks = []
        self.connections = []
        self.init_script = ""
        self.solver = {
            "method": "rk45",
            "startTime": 0.0,
            "stopTime": 10.0,
            "fixedStep": 0.001,
            "maxStep": 0.0,
            "minStep": 1e-12,
            "initialStep": 0.0,
            "relTol": 1e-4,
            "absTol": 1e-7,
            "maxLoggedSamples": 200000,
        }
        self.solver.update(solver)

    def add(self, block_id, type_name, name, x, y, width=80, height=60,
            mirrored=False, **params):
        self.blocks.append({
            "id": block_id,
            "type": type_name,
            "name": name,
            "geometry": {
                "x": x, "y": y, "width": width, "height": height,
                "rotation": 0, "mirrored": mirrored,
            },
            "params": params,
        })
        return block_id

    def name_signal(self, block_id, port, label):
        """Labels a signal, which is what the scope legend then shows."""
        for block in self.blocks:
            if block["id"] == block_id:
                block.setdefault("signals", {})[str(port)] = label
                return

    def wire(self, source, target, source_port=0, target_port=0):
        self.connections.append({
            "id": f"wire_{len(self.connections) + 1}",
            "source": {"block": source, "port": source_port},
            "target": {"block": target, "port": target_port},
        })

    def write(self, filename):
        document = {
            "format": "simupy-model",
            "version": 1,
            "name": self.name,
            "solver": self.solver,
            "blocks": self.blocks,
            "connections": self.connections,
        }
        if self.init_script:
            document["initScript"] = self.init_script

        path = HERE / filename
        path.write_text(json.dumps(document, indent=2) + "\n")
        print(f"wrote {path.relative_to(HERE.parent)}")

def mass_spring_damper():
    """A second-order plant driven by a step, shown against its input."""
    b = Builder("mass spring damper", stopTime=12.0)

    b.add("step", "Step", "Force", -260, 0, stepTime=1.0,
          initialValue=[0.0], finalValue=[1.0])
    b.add("plant", "TransferFcn", "Plant", -60, 0, width=130,
          numerator=[1.0], denominator=[1.0, 0.4, 4.0])
    b.add("mux", "Mux", "Mux", 140, 10, width=24, height=80, inputs=2.0)
    b.add("scope", "Scope", "Response", 240, 10, inputs=1.0, autoscale=True)

    b.wire("step", "plant")
    b.wire("plant", "mux", target_port=0)
    b.wire("step", "mux", target_port=1)
    b.wire("mux", "scope")
    b.write("mass_spring_damper.spy")

def pid_control_loop():
    """Classic unity-feedback loop: setpoint, PID, first-order plant."""
    b = Builder("pid control loop", stopTime=8.0, relTol=1e-6)

    b.add("setpoint", "Step", "Setpoint", -420, 0, stepTime=0.5,
          initialValue=[0.0], finalValue=[1.0])
    b.add("error", "Sum", "Error", -260, 10, width=60, height=60, signs="+-")
    b.add("pid", "PID", "Controller", -140, 0, P=4.0, I=6.0, D=0.4, N=50.0,
          limitOutput=True, upperLimit=8.0, lowerLimit=-8.0)
    b.add("plant", "TransferFcn", "Plant", 20, 0, width=130,
          numerator=[2.0], denominator=[1.0, 3.0, 2.0])
    b.add("mux", "Mux", "Mux", 220, 10, width=24, height=80, inputs=2.0)
    b.add("scope", "Scope", "Tracking", 320, 10, inputs=1.0)

    b.add("feedback", "Gain", "Sensor", 60, 170, width=70, height=50,
          mirrored=True, gain=[1.0])

    b.wire("setpoint", "error", target_port=0)
    b.wire("feedback", "error", target_port=1)
    b.wire("error", "pid")
    b.wire("pid", "plant")
    b.wire("plant", "feedback")
    b.wire("plant", "mux", target_port=0)
    b.wire("setpoint", "mux", target_port=1)
    b.wire("mux", "scope")
    b.write("pid_control_loop.spy")

def python_lorenz():
    """The Lorenz attractor as a single Python block."""
    b = Builder("lorenz attractor", stopTime=40.0, relTol=1e-9, absTol=1e-11)

    source = '''import numpy as np
from simupy import Block

class Lorenz(Block):
    """The Lorenz system, integrated by the C++ solver."""

    inputs = 0
    outputs = ["xyz"]
    states = 3
    direct_feedthrough = False

    def setup(self, widths):
        self.sigma = self.params.get("sigma", 10.0)
        self.rho = self.params.get("rho", 28.0)
        self.beta = self.params.get("beta", 8.0 / 3.0)
        return 3

    def initial_state(self):
        return [1.0, 1.0, 1.0]

    def output(self, t, u):
        return self.x

    def derivative(self, t, u):
        x, y, z = self.x
        return np.array([
            self.sigma * (y - x),
            x * (self.rho - z) - y,
            x * y - self.beta * z,
        ])
'''

    b.add("lorenz", "PythonFunction", "Lorenz", -200, 0, width=120, height=70,
          code=source, className="Lorenz",
          parameters="sigma = 10.0\nrho = 28.0\nbeta = 8.0 / 3.0")
    b.add("scope", "Scope", "States", 40, 0, inputs=1.0)

    b.wire("lorenz", "scope")
    b.write("python_lorenz.spy")

def python_controller():
    """A discrete Python controller driving a continuous C++ plant.

    Shows the two halves working together: the Python block runs on its own
    sample grid while the solver integrates the plant between hits.
    """
    b = Builder("python discrete controller", stopTime=6.0, relTol=1e-8)

    source = '''import numpy as np
from simupy import Block

class DiscretePI(Block):
    """A velocity-form PI controller with clamping, sampled at 50 Hz."""

    inputs = ["setpoint", "measurement"]
    outputs = ["command"]
    discrete_states = 1          # the accumulated integral term
    sample_time = 0.02
    direct_feedthrough = True

    def setup(self, widths):
        self.kp = self.params.get("kp", 3.0)
        self.ki = self.params.get("ki", 8.0)
        self.limit = self.params.get("limit", 5.0)
        return 1

    def output(self, t, u):
        error = u[0][0] - u[1][0]
        command = self.kp * error + self.xd[0]
        return np.clip(command, -self.limit, self.limit)

    def update(self, t, u):
        error = u[0][0] - u[1][0]
        integral = self.xd[0] + self.ki * self.sample_time * error

        # Stop winding up once the command is pinned against a limit.
        command = self.kp * error + integral
        if abs(command) > self.limit and command * error > 0:
            integral = self.xd[0]

        return [integral]
'''

    b.add("setpoint", "Step", "Setpoint", -460, 0, stepTime=0.5,
          initialValue=[0.0], finalValue=[1.0])
    b.add("ctrl", "PythonFunction", "PI (Python)", -280, 0, width=120,
          height=70, code=source, className="DiscretePI",
          parameters="kp = 3.0\nki = 8.0\nlimit = 5.0")
    b.add("plant", "TransferFcn", "Motor", -80, 0, width=130,
          numerator=[1.0], denominator=[0.5, 1.0])
    b.add("mux", "Mux", "Mux", 120, 10, width=24, height=80, inputs=2.0)
    b.add("scope", "Scope", "Tracking", 220, 10, inputs=1.0)
    b.add("feedback", "Gain", "Encoder", -100, 190, width=70, height=50,
          mirrored=True, gain=[1.0])

    b.wire("setpoint", "ctrl", target_port=0)
    b.wire("feedback", "ctrl", target_port=1)
    b.wire("ctrl", "plant")
    b.wire("plant", "feedback")
    b.wire("plant", "mux", target_port=0)
    b.wire("setpoint", "mux", target_port=1)
    b.wire("mux", "scope")
    b.write("python_discrete_controller.spy")

def bouncing_signals():
    """A tour of the source and math blocks, useful as a smoke test."""
    b = Builder("signal playground", stopTime=5.0)

    b.add("sine", "Sine", "Sine", -420, -120, amplitude=[1.0], frequency=3.0,
          frequencyUnit="rad/s", phase=0.0, bias=[0.0])
    b.add("pulse", "Pulse", "Pulse", -420, 20, amplitude=1.0, period=1.0,
          dutyCycle=40.0, phaseDelay=0.0)
    b.add("chirp", "Chirp", "Chirp", -420, 160, amplitude=1.0,
          startFrequency=0.2, endFrequency=4.0, sweepTime=5.0)

    b.add("sat", "Saturation", "Saturation", -240, 20, upperLimit=0.6,
          lowerLimit=-0.6)
    b.add("filter", "TransferFcn", "Low-pass", -240, 160, width=120,
          numerator=[1.0], denominator=[0.15, 1.0])

    b.add("mux", "Mux", "Mux", -40, 0, width=24, height=110, inputs=3.0)
    b.add("scope", "Scope", "Signals", 80, 20, inputs=1.0)

    b.wire("sine", "mux", target_port=0)
    b.wire("pulse", "sat")
    b.wire("sat", "mux", target_port=1)
    b.wire("chirp", "filter")
    b.wire("filter", "mux", target_port=2)
    b.wire("mux", "scope")
    b.write("signal_playground.spy")

def live_controls():
    """A loop you steer while it runs: real time, with three controls.

    The point of the model is not the control law — it is that the setpoint,
    the integral action and a disturbance are all driven from the Controls
    dock while the simulation is in flight.
    """
    b = Builder("live controls", stopTime=30.0, realTime=True,
                realTimeFactor=1.0, maxStep=0.02)

    b.add("setpoint", "Slider", "Setpoint", -560, 0, width=100, height=55,
          minimum=-2.0, maximum=2.0, value=1.0, step=0.05)
    b.name_signal("setpoint", 0, "setpoint")
    b.add("enable", "Toggle", "Integral action", -560, 150, width=100,
          height=55, on=True, offValue=0.0, onValue=1.0)
    b.add("kick", "PushButton", "Disturbance", -560, 300, width=100, height=55,
          offValue=0.0, onValue=0.6)

    b.add("error", "Sum", "Error", -380, 0, width=50, signs="+-")
    b.add("pid", "PID", "Controller", -280, 0, width=90, kp=3.0, ki=2.0, kd=0.2)
    b.add("gate", "Product", "Gate", -150, 0, width=50, inputs=2.0)
    b.add("inject", "Sum", "Injected", -50, 0, width=50, signs="++")
    b.add("plant", "TransferFcn", "Plant", 60, 0, width=120,
          numerator=[1.0], denominator=[1.0, 1.0])
    b.name_signal("plant", 0, "output")

    b.add("mux", "Mux", "Mux", 240, 0, width=24, height=90, inputs=2.0)
    b.add("scope", "Scope", "Tracking", 320, 0)

    b.wire("setpoint", "error", target_port=0)
    b.wire("plant", "error", target_port=1)
    b.wire("error", "pid")
    b.wire("pid", "gate", target_port=0)
    b.wire("enable", "gate", target_port=1)
    b.wire("gate", "inject", target_port=0)
    b.wire("kick", "inject", target_port=1)
    b.wire("inject", "plant")
    b.wire("setpoint", "mux", target_port=0)
    b.wire("plant", "mux", target_port=1)
    b.wire("mux", "scope")
    b.write("live_controls.spy")

if __name__ == "__main__":
    mass_spring_damper()
    pid_control_loop()
    python_lorenz()
    python_controller()
    bouncing_signals()
    live_controls()
