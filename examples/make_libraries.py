#!/usr/bin/env python3
"""Writes the shipped block libraries.

A `.spylib` is one JSON document holding every block it defines, so it is
easier to author here — with real Python source in triple-quoted strings —
than by hand.
"""

import json
import pathlib

HERE = pathlib.Path(__file__).parent
OUT = HERE.parent / "libraries"

def python_block(name, display, description, code, params=(), icon=None,
                 width=120.0, height=60.0, parameters=""):
    block = {
        "name": name,
        "displayName": display,
        "category": "Hardware",
        "description": description,
        "kind": "python",
        "width": width,
        "height": height,
        "params": list(params),
        "code": code,
        "parameters": parameters,
    }
    if icon:
        block["icon"] = {"kind": "text", "text": icon}
    return block

def real(name, label, value, tooltip=""):
    return {"name": name, "label": label, "kind": "real",
            "default": value, "tooltip": tooltip}

def text(name, label, value, tooltip=""):
    return {"name": name, "label": label, "kind": "text",
            "default": value, "tooltip": tooltip}

def integer(name, label, value, tooltip=""):
    return {"name": name, "label": label, "kind": "integer",
            "default": value, "tooltip": tooltip}

UDP_SEND = '''
import socket
import struct

from simupy import HardwareBlock

class UdpSend(HardwareBlock):
    """Sends the input as little-endian float64s to a UDP address.

    No dependencies and no wiring: the quickest way to get a signal out of
    SimuPy and into something else on the machine or the network.
    """

    inputs = ["u"]
    outputs = 0

    def open_device(self):
        return socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    def configure(self, widths):
        self.target = (self.params["host"], int(self.params["port"]))
        self.every = max(1, int(self.params.get("decimation", 1)))
        self.count = 0
        return None

    def output(self, t, u):
        # No side effects in output(): the solver calls it at trial states.
        return None

    def update(self, t, u):
        self.count += 1
        if self.count % self.every:
            return self.xd

        values = list(u[0])
        packet = struct.pack(f"<d{len(values)}d", t, *values)
        try:
            self.device.sendto(packet, self.target)
        except OSError as error:
            # Not fatal: the far end may simply not be listening yet.
            print(f"UdpSend: {error}")
        return self.xd

    discrete_states = 1
    direct_feedthrough = False
'''

UDP_RECEIVE = '''
import socket
import struct

import numpy as np
from simupy import HardwareBlock

class UdpReceive(HardwareBlock):
    """Reads little-endian float64s from a UDP port.

    Non-blocking: the newest datagram wins and the last value is held when
    nothing has arrived, so a slow or absent sender never stalls the solver.
    """

    inputs = 0
    outputs = ["y"]
    direct_feedthrough = False

    def open_device(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((self.params["host"], int(self.params["port"])))
        sock.setblocking(False)
        return sock

    def configure(self, widths):
        self.width = max(1, int(self.params["width"]))
        self.value = np.zeros(self.width)
        return self.width

    def output(self, t, u):
        # Drained, not read once: only the freshest datagram matters.
        while True:
            try:
                packet, _ = self.device.recvfrom(65535)
            except BlockingIOError:
                break
            except OSError:
                break

            count = len(packet) // 8 - 1
            if count < 1:
                continue
            fields = struct.unpack(f"<d{count}d", packet[: (count + 1) * 8])
            incoming = np.asarray(fields[1:], dtype=float)
            take = min(self.width, incoming.size)
            self.value[:take] = incoming[:take]

        return self.value
'''

SERIAL_SEND = '''
import struct

from simupy import HardwareBlock, require

class SerialSend(HardwareBlock):
    """Writes the input to a serial port — an Arduino, a motor driver, a PLC.

    One line of ASCII per accepted step by default, which is what an Arduino
    sketch using ``Serial.parseFloat()`` expects. Switch ``format`` to
    ``binary`` for packed float64s when the far end can read them.
    """

    inputs = ["u"]
    outputs = 0
    discrete_states = 1
    direct_feedthrough = False

    def open_device(self):
        serial = require("serial", "pyserial", "talking to a serial device")
        return serial.Serial(
            port=self.params["port"],
            baudrate=int(self.params["baud"]),
            timeout=0,          # never block the solver on a write
            write_timeout=0.05,
        )

    def configure(self, widths):
        self.binary = str(self.params.get("format", "text")) == "binary"
        self.every = max(1, int(self.params.get("decimation", 1)))
        self.count = 0
        return None

    def output(self, t, u):
        return None

    def update(self, t, u):
        self.count += 1
        if self.count % self.every:
            return self.xd

        values = list(u[0])
        try:
            if self.binary:
                self.device.write(struct.pack(f"<{len(values)}d", *values))
            else:
                line = " ".join(f"{v:.6g}" for v in values)
                self.device.write((line + "\\n").encode("ascii"))
        except Exception as error:  # pyserial raises several unrelated types
            print(f"SerialSend: {error}")
        return self.xd
'''

SERIAL_RECEIVE = '''
import threading

import numpy as np
from simupy import HardwareBlock, require

class SerialReceive(HardwareBlock):
    """Reads whitespace-separated numbers from a serial port, one line each.

    The port is read by a background thread, not by ``output()``. A blocking
    read in the solver loop would make the simulation late by however long the
    device took to answer; this way the thread waits and the solver always
    gets the most recent complete line.

    That thread needs Python's global lock, which SimuPy hands back while a
    real-time run waits between steps — so this works properly in a paced run
    and is of limited use in one going flat out.
    """

    inputs = 0
    outputs = ["y"]
    direct_feedthrough = False

    def open_device(self):
        serial = require("serial", "pyserial", "reading from a serial device")
        return serial.Serial(
            port=self.params["port"],
            baudrate=int(self.params["baud"]),
            timeout=0.1,
        )

    def configure(self, widths):
        self.width = max(1, int(self.params["width"]))
        self.value = np.zeros(self.width)
        self.lock = threading.Lock()
        self.running = True
        self.reader = threading.Thread(target=self._read_loop, daemon=True)
        self.reader.start()
        return self.width

    def _read_loop(self):
        while self.running:
            try:
                line = self.device.readline()
            except Exception:
                break
            if not line:
                continue
            try:
                fields = [float(f) for f in line.split()]
            except ValueError:
                continue        # a partial or noisy line; wait for the next
            if not fields:
                continue
            with self.lock:
                take = min(self.width, len(fields))
                self.value[:take] = fields[:take]

    def output(self, t, u):
        with self.lock:
            return self.value.copy()

    def close_device(self, device):
        # Stop the thread before the port it is reading disappears.
        self.running = False
        if self.reader.is_alive():
            self.reader.join(timeout=1.0)
        device.close()
'''

ROS2_PUBLISH = '''
import threading

from simupy import HardwareBlock, require

class Ros2Publish(HardwareBlock):
    """Publishes the input on a ROS 2 topic as std_msgs/Float64MultiArray.

    Needs a sourced ROS 2 installation on PYTHONPATH; SimuPy does not bundle
    one. Start from this and change the message type to whatever your graph
    actually speaks — the shape of the block stays the same.
    """

    inputs = ["u"]
    outputs = 0
    discrete_states = 1
    direct_feedthrough = False

    def open_device(self):
        rclpy = require("rclpy", "rclpy", "publishing to ROS 2")
        msgs = require("std_msgs.msg", "std_msgs", "ROS 2 message types")

        if not rclpy.ok():
            rclpy.init()
        node = rclpy.create_node(str(self.params["node"]))
        self.publisher = node.create_publisher(
            msgs.Float64MultiArray, str(self.params["topic"]), 10)
        self.message = msgs.Float64MultiArray

        # Its own thread, so callbacks are serviced between solver steps.
        self.rclpy = rclpy
        self.running = True
        self.spinner = threading.Thread(
            target=lambda: rclpy.spin(node), daemon=True)
        self.spinner.start()
        return node

    def configure(self, widths):
        self.every = max(1, int(self.params.get("decimation", 1)))
        self.count = 0
        return None

    def output(self, t, u):
        return None

    def update(self, t, u):
        self.count += 1
        if self.count % self.every:
            return self.xd
        message = self.message()
        message.data = [float(v) for v in u[0]]
        self.publisher.publish(message)
        return self.xd

    def close_device(self, device):
        self.running = False
        device.destroy_node()
        # Not rclpy.shutdown(): another block may share the context.
'''

ANALOG_READ = """
from simupy import ArduinoBlock

class ArduinoAnalogRead(ArduinoBlock):
    \"\"\"Reads an analog input, streamed continuously by the board.

    The value is whatever arrived most recently — never a round trip taken
    inside the solver loop, which at 115200 baud would cost about a
    millisecond per step.
    \"\"\"

    outputs = ["y"]

    def configure(self, widths):
        self.pin = int(self.params["pin"])
        self.scale = float(self.params.get("fullScale", 1.0))
        self.offset = float(self.params.get("offset", 0.0))
        return 1

    def output(self, t, u):
        # Opens the link and registers the pin; setup() runs several times.
        board = self.device
        board.add_input(self.pin, analog=True)
        return board.read(self.pin, analog=True) * self.scale + self.offset
"""

DIGITAL_READ = """
from simupy import ArduinoBlock

class ArduinoDigitalRead(ArduinoBlock):
    \"\"\"Reads a digital input, streamed continuously by the board.\"\"\"

    outputs = ["y"]

    def configure(self, widths):
        self.pin = int(self.params["pin"])
        self.pullup = str(self.params.get("mode", "input")) == "pullup"
        self.armed = False
        return 1

    def output(self, t, u):
        board = self.device
        if not self.armed:
            board.set_mode(self.pin, "U" if self.pullup else "I")
            self.armed = True
        board.add_input(self.pin, analog=False)
        return board.read(self.pin, analog=False)
"""

ANALOG_WRITE = """
from simupy import ArduinoBlock

class ArduinoAnalogWrite(ArduinoBlock):
    \"\"\"Drives a PWM pin from the input, as a duty between 0 and 1.

    The board scales the duty to its own PWM width, so the same model drives
    an Uno's 8 bits and an ESP32's 12 without knowing which it is talking to.
    \"\"\"

    inputs = ["u"]
    discrete_states = 1

    def configure(self, widths):
        self.pin = int(self.params["pin"])
        self.scale = float(self.params.get("fullScale", 1.0))
        self.last = None
        return None

    def output(self, t, u):
        # No side effects in output(): a pin cannot be un-driven.
        return None

    def update(self, t, u):
        duty = float(u[0][0]) / self.scale if self.scale else 0.0
        duty = 0.0 if duty < 0.0 else (1.0 if duty > 1.0 else duty)
        # Quantised, so noise does not fill the link with identical commands.
        step = round(duty * 1000.0)
        if step != self.last:
            self.last = step
            self.device.write_pwm(self.pin, step / 1000.0)
        return self.xd
"""

DIGITAL_WRITE = """
from simupy import ArduinoBlock

class ArduinoDigitalWrite(ArduinoBlock):
    \"\"\"Drives a digital output: high when the input is above the threshold.\"\"\"

    inputs = ["u"]
    discrete_states = 1
    zero_crossings = 1

    def configure(self, widths):
        self.pin = int(self.params["pin"])
        self.threshold = float(self.params.get("threshold", 0.5))
        self.last = None
        return None

    def output(self, t, u):
        return None

    def zero_crossing(self, t, u):
        # Declared so the solver lands a step on the switching instant.
        return [float(u[0][0]) - self.threshold]

    def update(self, t, u):
        level = 1 if float(u[0][0]) > self.threshold else 0
        if level != self.last:
            self.last = level
            self.device.write_digital(self.pin, level)
        return self.xd
"""

def arduino_common(pin_default, extra=()):
    """Port, baud and rate are the same on every Arduino block."""
    return [
        text("port", "Port", "/dev/ttyUSB0",
             "Serial device: /dev/ttyUSB0 or /dev/ttyACM0 on Linux, COM3 on "
             "Windows. Blocks sharing a port share one connection."),
        integer("baud", "Baud rate", 115200),
        integer("pin", "Pin", pin_default),
        integer("rate", "Stream rate (Hz)", 200,
                "How often the board sends its inputs. Set once per board."),
        *extra,
    ]

def hardware_library():
    return {
        "format": "simupy-library",
        "version": 1,
        "name": "Hardware",
        "revision": 1,
        "author": "SimuPy",
        "description": (
            "Blocks that talk to things outside the process: UDP, serial "
            "(Arduino), ROS 2. Each names the package it needs, and none is "
            "required to use SimuPy."
        ),
        "blocks": [
            python_block(
                "UdpSend", "UDP send",
                "Sends the input as float64s to a UDP address. No "
                "dependencies — the quickest way out of the process.",
                UDP_SEND, icon="UDP▶",
                params=[
                    text("host", "Host", "127.0.0.1"),
                    integer("port", "Port", 5005),
                    integer("decimation", "Send every N steps", 1,
                            "Raise it when the far end cannot keep up."),
                ]),
            python_block(
                "UdpReceive", "UDP receive",
                "Reads float64s from a UDP port, non-blocking. Holds the last "
                "value when nothing arrives.",
                UDP_RECEIVE, icon="▶UDP",
                params=[
                    text("host", "Bind address", "0.0.0.0"),
                    integer("port", "Port", 5006),
                    integer("width", "Signal width", 1),
                ]),
            python_block(
                "SerialSend", "Serial send",
                "Writes the input to a serial port — an Arduino, a driver, a "
                "PLC. Needs pyserial.",
                SERIAL_SEND, icon="TX",
                params=[
                    text("port", "Device", "/dev/ttyUSB0"),
                    integer("baud", "Baud rate", 115200),
                    text("format", "Format", "text",
                         "'text' for one line per step, 'binary' for packed "
                         "float64s."),
                    integer("decimation", "Send every N steps", 1),
                ]),
            python_block(
                "SerialReceive", "Serial receive",
                "Reads numbers from a serial port on a background thread, so "
                "a slow device never delays the solver. Needs pyserial.",
                SERIAL_RECEIVE, icon="RX",
                params=[
                    text("port", "Device", "/dev/ttyUSB0"),
                    integer("baud", "Baud rate", 115200),
                    integer("width", "Signal width", 1),
                ]),
            python_block(
                "Ros2Publish", "ROS 2 publish",
                "Publishes the input on a ROS 2 topic. Needs a sourced ROS 2 "
                "installation; start from this and change the message type.",
                ROS2_PUBLISH, icon="ROS▶",
                params=[
                    text("topic", "Topic", "/simupy/out"),
                    text("node", "Node name", "simupy"),
                    integer("decimation", "Publish every N steps", 1),
                ]),
            python_block(
                "ArduinoAnalogRead", "Arduino analogRead",
                "Reads an analog pin from a board running the simupy_bridge "
                "firmware. Output is 0..1 scaled by 'Full scale'.",
                ANALOG_READ, icon="A→", width=140.0,
                params=arduino_common(0, [
                    real("fullScale", "Full scale", 1.0,
                         "Multiplies the 0..1 reading. Set it to the board's "
                         "reference voltage to read volts."),
                    real("offset", "Offset", 0.0),
                ])),
            python_block(
                "ArduinoDigitalRead", "Arduino digitalRead",
                "Reads a digital pin from a board running the simupy_bridge "
                "firmware.",
                DIGITAL_READ, icon="D→", width=140.0,
                params=arduino_common(2, [
                    text("mode", "Input mode", "input",
                         "'input' or 'pullup'."),
                ])),
            python_block(
                "ArduinoAnalogWrite", "Arduino analogWrite",
                "Drives a PWM pin. The input is a duty between 0 and 'Full "
                "scale'; the board scales it to its own PWM width.",
                ANALOG_WRITE, icon="→P", width=140.0,
                params=arduino_common(9, [
                    real("fullScale", "Full scale", 1.0,
                         "The input value that means fully on."),
                ])),
            python_block(
                "ArduinoDigitalWrite", "Arduino digitalWrite",
                "Drives a digital pin high while the input is above the "
                "threshold. Declares a zero crossing, so the pin changes at "
                "the instant the model says it does.",
                DIGITAL_WRITE, icon="→D", width=140.0,
                params=arduino_common(13, [
                    real("threshold", "Threshold", 0.5),
                ])),
        ],
    }

if __name__ == "__main__":
    OUT.mkdir(exist_ok=True)
    path = OUT / "hardware.spylib"
    path.write_text(json.dumps(hardware_library(), indent=2) + "\n")
    print(f"wrote {path.relative_to(HERE.parent)}")
