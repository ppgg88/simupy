==========================
Driving a model as it runs
==========================

A real-time run you can only watch is a film. The **Controls** category adds
three blocks you drive while the simulation is in flight:

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Block
     - What it does
   * - ``Slider``
     - A value dragged between two bounds — a setpoint, a gain, a load.
   * - ``Toggle``
     - Two values, latched — an enable, a mode, a fault to inject.
   * - ``PushButton``
     - Its on value only while held — a disturbance, a reset pulse.

Two ways to drive them
======================

From the **Controls** dock, where every control in the model appears labelled
by its path through the hierarchy. A control three subsystems deep is still
something you want to reach without navigating to it.

Or **on the block itself**: click the switch to flip it, hold the button face
to press it, drag the slider's track to move it. The cursor changes over any
part of a block that can be driven.

.. note::

   The zone that responds is deliberately smaller than the block, so dragging
   anywhere else still moves the block around the canvas. Both routes write to
   the same value, so the dock and the diagram always agree.

The Controls dock is the one panel that stays live during a run. The canvas is
locked rather than disabled — it still pans, zooms and takes control input,
but nothing can be moved, wired, deleted or pasted. The library and the
property panel are disabled outright, because editing a model mid-run is not a
thing; driving one is.

Initial conditions
==================

A control set *before* pressing :guilabel:`Run` starts the run in that state.
Flipping a switch and then starting is how you set an initial condition, so
the run seeds itself from the parameters only when nobody has touched the
control.

Whatever the controls are left at when the run ends becomes the model's own
value, so a setpoint tuned mid-run is still there when you save.

How it crosses the thread boundary
==================================

The solver runs on a worker thread and the interface on the main one, so a
control's value lives in a ``std::atomic`` on the block. That is the only
thing the two threads share: parameters are read once at setup and never
again, so moving a slider cannot race with anything the solver is doing.

.. warning::

   A change lands at the **next evaluation**. With a variable-step solver that
   means it takes effect somewhere inside the step in progress rather than at
   a declared instant — the solver has no way to know a hand was about to
   move.

   For a slider that is invisible. For a toggle or a button it is a genuine
   discontinuity crossed blind, so if the exact switching instant matters, cap
   the maximum step to bound how far off it can be.

Example
=======

``examples/live_controls.spy`` is a loop built to be steered: move the
setpoint and watch it track, turn off the integral action and watch the offset
appear, hold the disturbance button and watch it rejected.
