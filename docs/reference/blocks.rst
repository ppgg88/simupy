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
     - ``Integrator``, ``Derivative``, ``TransferFcn``,
       ``AdaptiveTransferFcn``, ``StateSpace``, ``PID``
   * - Discrete
     - ``UnitDelay``, ``ZeroOrderHold``, ``DiscreteIntegrator``,
       ``DiscreteTransferFcn``, ``DiscreteAdaptiveTransferFcn``,
       ``IntegerDelay``
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

``AdaptiveTransferFcn``
   Identifies a plant in the Laplace domain while the model runs. Feed it the
   signal driving the plant on ``u`` and the plant's measured response on
   ``d``; it adjusts an order-N model until its own output ``y`` matches
   ``d``, leaving the error on ``e`` and the coefficients on ``w``.

   The weights come out as ``[a1..aN, b0, b1..bN]`` — ``b0`` only when
   ``Adapt a direct term`` is on — which is the denominator ``[1, a1..aN]``
   and numerator ``[b0..bN]`` of a ``TransferFcn``, in descending powers of
   ``s``.

   A measured signal cannot be differentiated, so both sides of the equation
   are passed through a filter of bandwidth ``State filter cutoff``. Set it
   well above the plant's own dynamics — a decade is a good start — and below
   the noise you would rather not amplify. Too low and the fit ends up
   describing the filter instead of the plant.

   ``rls`` converges in less time than ``gradient``, but its accuracy is
   capped by ``RLS initial covariance``: that number is the weight given to
   the starting guess, so leaving it small strands the fit part way to the
   answer. Raise the forgetting rate above 0 only to track a plant that
   drifts.

``DiscreteAdaptiveTransferFcn``
   The same idea in the z domain, fitted per sample rather than continuously.
   Feed it the signal driving the
   plant on ``u`` and the plant's measured response on ``d``; it adjusts an
   order-N model until its own output ``y`` matches ``d``, leaving the error
   on ``e`` and the coefficients on ``w``.

   The weights come out as ``[a1..aN, b0, b1..bN]`` — ``b0`` only when
   ``Adapt a direct term`` is on — which is the denominator ``[1, a1..aN]``
   and numerator ``[b0..bN]`` of a ``DiscreteTransferFcn`` at the same sample
   time. Reading them off a ``Display`` at the end of a run is the usual way
   to turn a measurement into a fixed block.

   Nothing is identified from a signal that does not move: the weights only
   go where the input excites them, so use a signal rich in frequencies —
   ``RandomNumber``, ``Chirp``, or a PRBS — rather than a step or a single
   sine. ``rls`` converges in far fewer samples than ``nlms``, at N² work per
   sample; drop its forgetting factor below 1 to track a plant that drifts.

   ``Regressor feedback`` decides what the model is fitted against.
   ``measured`` fits the one step ahead prediction and cannot run away, which
   is what you want while the plant is noisy. ``model`` fits the free running
   simulation, which is closer to the transfer function you will keep, but it
   can diverge if the fit passes through an unstable region.

``Display``
   Shows the value on the block itself, refreshed as the run goes and left on
   screen when it ends. It reads ``—`` until the first step of a run. A wide
   signal is listed one element per line, and a signal too wide for the block
   is cut short with a ``+N more`` line — drag the block bigger to see the
   rest.

``Goto`` / ``From``
   Splice points rather than computations. See :doc:`../guide/signals`.

``Slider`` / ``Toggle`` / ``PushButton``
   Driven from the Controls dock or on the block itself. See
   :doc:`../guide/controls`.

``Scope``
   ``Time window (s)`` keeps a fixed span of history on screen while the model
   runs, 0 showing the whole run. See :doc:`../guide/editor`.

``Subsystem``
   Expanded before compilation, so it costs nothing at run time. See
   :doc:`../guide/subsystems`.

Shipped in the hardware library
===============================

``ArduinoAnalogRead``, ``ArduinoAnalogWrite``, ``ArduinoDigitalRead``,
``ArduinoDigitalWrite``, ``UdpSend``, ``UdpReceive``, ``SerialSend``,
``SerialReceive``, ``Ros2Publish`` — see :doc:`../guide/hardware`.
