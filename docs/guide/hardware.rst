=====================
Talking to hardware
=====================

Nothing special is needed: a Python block can ``import serial`` and drive an
Arduino, or ``import rclpy`` and publish to ROS 2. What the project adds is
the handful of things that make it work rather than nearly work.

Optional dependencies stay optional
===================================

``require()`` imports one and, when it is missing, says which package and why
instead of throwing a traceback through SimuPy's own machinery:

.. code-block:: python

   serial = require("serial", "pyserial", "talking to an Arduino")

.. code-block:: text

   ImportError: this block needs the 'serial' module (talking to an Arduino),
   which is not installed.
   Install it with:  pip install pyserial

``HardwareBlock``
=================

Subclass it, implement ``open_device()`` and ``close_device()``, use
``self.device``. The device opens on first use and closes exactly once however
the run ended — and a failure to close is reported rather than allowed to mask
whatever actually went wrong.

.. important::

   Opening lazily is not an accident. Signal widths propagate to a fixpoint,
   so the solver calls ``setup()`` on a fresh instance several times per run
   and keeps only the last. A port opened in ``setup()`` would be opened once
   per attempt — the second failing with the device busy — and the discarded
   instances would never close theirs.

Blocking reads belong on a thread
=================================

A run holds Python's global lock from start to finish, and SimuPy hands it
back while a real-time run waits between steps — which is where nearly all the
wall clock goes. A background thread reading a serial port therefore gets the
time it needs, and ``output()`` returns the value it last left behind instead
of making the simulation late by however long the device took to answer.

``SerialReceive`` in the shipped library is written this way and is worth
reading before writing your own.

.. tip::

   Pair hardware with real time. A model driving hardware flat out will send a
   year of setpoints in a second — see :doc:`simulation`.

The shipped library
===================

``libraries/hardware.spylib``. Install it with :menuselection:`Library -->
Manage Libraries --> Install`, or point ``SIMUPY_LIBRARY_PATH`` at the
``libraries/`` directory.

.. list-table::
   :header-rows: 1
   :widths: 34 16 50

   * - Block
     - Needs
     - Notes
   * - ``ArduinoAnalogRead`` / ``Write``,
       ``ArduinoDigitalRead`` / ``Write``
     - ``pyserial``
     - A board running the bundled firmware; see below.
   * - ``UdpSend`` / ``UdpReceive``
     - nothing
     - float64s over a socket. The receiver is non-blocking and holds its
       last value.
   * - ``SerialSend`` / ``SerialReceive``
     - ``pyserial``
     - Text lines by default, which is what ``Serial.parseFloat()`` expects.
   * - ``Ros2Publish``
     - a sourced ROS 2
     - ``std_msgs/Float64MultiArray``; a starting point to change the message
       type on.

The UDP pair depends on nothing at all, which makes it the quickest way to
check the path works — and it is what the test suite exercises, through a real
socket, so the send, the bind, the non-blocking drain and the lifecycle are
all genuinely covered without any hardware present.

Arduino and ESP32
=================

Flash ``firmware/simupy_bridge/simupy_bridge.ino`` once and the board's pins
become blocks. Nothing on the board is model-specific: the model says at run
time which pins to read and write.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Block
     - Does
   * - ``ArduinoAnalogRead``
     - Reads an analog pin, 0..1 scaled by *Full scale*.
   * - ``ArduinoDigitalRead``
     - Reads a digital pin, with optional pull-up.
   * - ``ArduinoAnalogWrite``
     - Drives a PWM pin from a 0..1 duty.
   * - ``ArduinoDigitalWrite``
     - Drives a pin high above a threshold, with a declared zero crossing so
       it switches at the instant the model says.

Set every block's :guilabel:`Port` to the same device and they share one
connection — the board object is reference counted per port, because four
blocks opening four handles on one ``/dev/ttyUSB0`` would simply fight.

.. important::

   The board streams; the host never asks. It sends the configured input pins
   continuously at a rate the model sets, and a block's ``output()`` reads the
   newest sample from a local cache. A request-response per pin per step would
   cost about a millisecond of round trip at 115200 baud, which a control loop
   cannot spend.

Duty cycles cross the wire as 0..1 and the board scales them to its own PWM
width, so the same model drives an Uno's 8 bits and an ESP32's 12 without
knowing which it is talking to. Analog readings come back as raw counts and
are normalised using the resolution the board reported when it identified
itself.

Supported boards
----------------

AVR (Uno, Nano, Mega, Leonardo, Micro), SAMD, RP2040, ESP8266, and ESP32 on
both the 2.x and 3.x cores — the 2.x core has no ``analogWrite``, so the
sketch falls back to LEDC. Everything board-specific is in one HAL section at
the top of the sketch; the protocol below it is identical everywhere.

Protocol
--------

ASCII lines, ``\n`` terminated. Host to board:

.. list-table::
   :header-rows: 1
   :widths: 26 74

   * - Command
     - Meaning
   * - ``?``
     - Identify.
   * - ``C <pin> <pin> …``
     - Set the streamed input list, in this order. No pins stops the stream.
   * - ``R <hz>``
     - Stream rate, 1..2000 Hz.
   * - ``M <pin> <mode>``
     - Pin mode: ``I`` input, ``U`` pull-up, ``O`` output.
   * - ``W <pin> <0|1>``
     - ``digitalWrite``.
   * - ``P <pin> <duty>``
     - ``analogWrite``, duty 0..1 as a decimal.

Board to host:

.. list-table::
   :header-rows: 1
   :widths: 40 60

   * - Line
     - Meaning
   * - ``#simupy <ver> <board> <adcBits> <pwmBits> …``
     - Identification.
   * - ``#C <gen>``
     - A ``C`` was accepted, under this generation.
   * - ``!<message>``
     - Something was wrong with a command.
   * - ``<gen> <micros> <v> <v> …``
     - A sample of the configured pins.

.. note::

   ``gen`` counts configuration changes, and the board reports it in the
   ``#C`` acknowledgement rather than leaving the host to guess. A board only
   starts counting from zero if it reset when the port opened — which an Uno
   does and an ESP32 does not, and which nothing does on a second run against
   a board already powered.

Trying it without a board
=========================

.. code-block:: console

   $ python3 tools/fake_arduino.py --mode echo
   /dev/pts/7

That is a board, in Python, speaking the same protocol on a pseudo-terminal.
Point the blocks' :guilabel:`Port` at the path it prints and the model runs
exactly as it would over USB — useful for building a model before the hardware
arrives, and what the test suite uses to cover the host side without any.

``--mode echo`` makes analog input *n* read back whatever was last written to
PWM pin *n*, which closes a loop through the "board"; the default reads a slow
sine on every input so a plot shows something recognisable.

.. warning::

   The sketch itself has never been run on hardware by the authors — only the
   host side is covered by tests, against the simulated board. That the
   firmware implements the protocol identically is the part to confirm at the
   first connection.
