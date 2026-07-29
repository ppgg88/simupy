===============
Block reference
===============

Every block's parameters are listed by ``simupy-cli blocks``, and shown with
their tooltips in the property panel.

Built in
========

.. list-table::
   :header-rows: 1
   :widths: 18 82

   * - Category
     - Blocks
   * - Sources
     - ``Constant``, ``Step``, ``Ramp``, ``Sine``, ``Clock``, ``Pulse``,
       ``Chirp``, ``RandomNumber``
   * - Math
     - ``Gain``, ``Sum``, ``Product``, ``Abs``, ``Sign``, ``MathFunction``,
       ``Trigonometry``, ``Saturation``, ``DeadZone``, ``MinMax``,
       ``VectorReduce``
   * - Logic
     - ``Relational``, ``Logic``
   * - Continuous
     - ``Integrator``, ``Derivative``, ``TransferFcn``, ``StateSpace``,
       ``PID``
   * - Discrete
     - ``UnitDelay``, ``ZeroOrderHold``, ``DiscreteIntegrator``,
       ``DiscreteTransferFcn``, ``IntegerDelay``
   * - Routing
     - ``Mux``, ``Demux``, ``Selector``, ``Switch``, ``ManualSwitch``,
       ``Goto``, ``From``
   * - Controls
     - ``Slider``, ``Toggle``, ``PushButton``
   * - Sinks
     - ``Scope``, ``Display``, ``Terminator``, ``ToFile``
   * - Ports
     - ``Subsystem``, ``Inport``, ``Outport``
   * - Python
     - ``PythonFunction``

Plus whatever installed libraries add — see :doc:`../guide/libraries`.

Notes on particular blocks
==========================

``Derivative``
   A plain finite difference between major steps: first order, and it
   amplifies noise. Prefer a filtered derivative, ``s/(tau*s + 1)`` as a
   ``TransferFcn``, where accuracy matters.

``Goto`` / ``From``
   Splice points rather than computations. See :doc:`../guide/signals`.

``Slider`` / ``Toggle`` / ``PushButton``
   Driven from the Controls dock or on the block itself. See
   :doc:`../guide/controls`.

``Subsystem``
   Expanded before compilation, so it costs nothing at run time. See
   :doc:`../guide/subsystems`.

Shipped in the hardware library
===============================

``ArduinoAnalogRead``, ``ArduinoAnalogWrite``, ``ArduinoDigitalRead``,
``ArduinoDigitalWrite``, ``UdpSend``, ``UdpReceive``, ``SerialSend``,
``SerialReceive``, ``Ros2Publish`` — see :doc:`../guide/hardware`.
