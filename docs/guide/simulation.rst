=================
Running a model
=================

Choosing a solver
=================

.. list-table::
   :header-rows: 1
   :widths: 22 10 68

   * - Method
     - Order
     - Notes
   * - Dormand-Prince 5(4)
     - 5
     - Adaptive step with error control. The default.
   * - SDIRK2
     - 2
     - Implicit and L-stable, for stiff models. Newton per stage with a
       numerically differenced Jacobian.
   * - Runge-Kutta 4
     - 4
     - Fixed step.
   * - Heun
     - 2
     - Fixed step.
   * - Euler
     - 1
     - Fixed step.
   * - Discrete
     - —
     - For models with no continuous states.

Stiffness is what decides between the first two. On a plant with poles at −1
and −10⁵ at a relative tolerance of 1e-6, SDIRK2 finishes in 3 100 derivative
evaluations where Dormand-Prince needs 373 000 — the explicit method is held
back by stability, not accuracy. At tight tolerances on a well-conditioned
model the ranking reverses, because a second-order method needs small steps
whatever its stability region.

There is no automatic stiffness detection; the choice is yours.

Running in real time
====================

By default a run finishes as fast as the machine allows — eight seconds of
model time in seventy milliseconds. That is what you want when you are after a
final number, and the wrong thing entirely when you are watching a scope,
demonstrating a controller, or driving something outside the process.

The **clock button** on the toolbar paces the run against the wall clock.
:menuselection:`Simulation --> Settings --> Pacing` sets the speed: ``1x`` is
real time, ``0.25x`` quarter speed for something that happens too fast to see,
``10x`` when real time is more patience than the model deserves. The setting
is saved with the model.

.. important::

   Pacing changes the speed, not the answer. The solver takes exactly the same
   steps and produces bit-identical results; the run simply waits between them.

Deadlines are absolute rather than incremental — each is computed from the
start of the run, not from the previous one — so a step that overruns is
absorbed instead of pushing every later step further out. A run that cannot
compute fast enough falls behind rather than skipping work, and the summary
says so:

.. code-block:: text

   Finished at t = 8 s — 235 steps … — real time held, worst lag 0.4 ms
   Finished at t = 8 s — 235 steps … — COULD NOT KEEP UP: worst lag 1.8 s,
                                       resynced 2 times

Past a second of lag the pacer restarts its clock rather than spending the
rest of the run trying to catch up — which is also what absorbs a laptop
suspending mid-run. Every restart is counted and reported, so "real time held"
means it actually was.

From the shell:

.. code-block:: console

   $ simupy-cli run model.spy --realtime        # 1x
   $ simupy-cli run model.spy --realtime 0.25   # quarter speed

Running with no end
===================

The **∞ button** beside the stop time drops the end time entirely: the run
keeps going until :guilabel:`Stop` is pressed. It pairs naturally with real
time — a console you leave open and steer — and the two toggles sit side by
side for that reason. An unbounded run at full speed is legal but simply burns
a core.

.. note::

   Memory stays bounded. The log decimates itself as it fills, halving in
   place, so the samples thin out but the trace still spans the whole run. A
   nine-second unbounded run of ``mass_spring_damper.spy`` at full speed
   reaches 1.25 million simulated seconds and holds flat at 40 MB.

The stop time is kept rather than cleared, so switching ∞ back off returns the
model to the end time it had.

.. code-block:: console

   $ simupy-cli run model.spy --unbounded       # until Ctrl+C

Ctrl+C on an unbounded run is a clean stop, not a kill: the run ends at the
next major step, and the summary and the ``--csv`` file are still written.

Logging
=======

Logging is tied to accepted solver steps. That is what makes a variable-step
plot dense through a fast transient and sparse through a quiet stretch. The
log decimates itself once it hits the sample cap, halving in place, so a long
run stays within bounded memory without losing its shape.

The cap is :menuselection:`Simulation --> Settings --> Data logging --> Max
samples per signal`.
