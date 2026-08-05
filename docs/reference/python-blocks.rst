=============
Python blocks
=============

Drop a :guilabel:`Python → PythonFunction` block on the canvas and
double-click it. Subclass ``simupy.Block``, declare the shape of the block
with class attributes, and implement the methods the solver calls.

.. code-block:: python

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

Class attributes
================

.. list-table::
   :header-rows: 1
   :widths: 26 74

   * - Attribute
     - Meaning
   * - ``inputs``, ``outputs``
     - A count, or a list of port names.
   * - ``states``
     - Continuous states, integrated by the solver.
   * - ``discrete_states``
     - Discrete states, advanced by ``update()`` on sample hits.
   * - ``sample_time``
     - A period in seconds. Makes the block discrete.
   * - ``direct_feedthrough``
     - Whether ``output()`` reads its inputs. ``False`` is what lets a block
       sit inside a feedback loop without creating an algebraic one.
   * - ``zero_crossings``
     - How many discontinuity functions ``zero_crossing()`` returns.

Methods
=======

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Method
     - Called
   * - ``setup(widths)``
     - Once per compile, possibly several times as widths settle. Return the
       output width, a list of widths, or ``None`` to inherit.
   * - ``initial_state()``
     - Once, for the continuous state.
   * - ``output(t, u)``
     - Whenever the solver needs an output. **Must have no side effects.**
   * - ``derivative(t, u)``
     - When ``states > 0``.
   * - ``update(t, u)``
     - On each sample hit, after ``output()``. Returns the next discrete state.
   * - ``zero_crossing(t, u)``
     - Right after ``output()``, when ``zero_crossings > 0``.
   * - ``terminate()``
     - Once when the run ends.

State and parameters
====================

``self.x`` (continuous) and ``self.xd`` (discrete) are NumPy arrays that
**view the engine's own buffers** — reading them copies nothing. They are
read-only; write state by returning it from ``derivative()`` or ``update()``.

``u`` is always a list of arrays, one per input port. Return a scalar or array
for a single output, or a list of them for several.

.. warning::

   The arrays in ``u`` are views onto the engine's signal buffers and are
   valid **for the duration of the call only**. Keeping one — the classic
   ``self.history.append(u[0])`` — stores a window onto memory the engine
   reuses on the next step, so what you read back later is not what you put
   there. Copy anything you intend to keep::

       self.history.append(u[0].copy())

   ``self.x`` and ``self.xd`` do survive being kept this way, but ``.copy()``
   is the habit worth having for all of them.

``self.params`` is a dict built from the block's :guilabel:`Parameters` field,
which is plain Python (``tau = 0.5``, ``gains = np.array([1, 2, 3])``), so the
same script can be reused with different values. A block saved into a library
with a mask gets those parameters here too — see :doc:`../guide/libraries`.

Discrete blocks
===============

Set ``sample_time`` to a period in seconds and implement ``update()``. The
block is then evaluated only on its own sample grid; the solver still
integrates the continuous parts of the model in between, and steps exactly
onto each sample instant.

.. code-block:: python

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

Discontinuous blocks
====================

If a block switches — a threshold, a limit, a sign change — declare where, and
the solver will land its step exactly on the event instead of integrating
across it:

.. code-block:: python

   class Relay(Block):
       inputs = ["u"]
       outputs = ["y"]
       zero_crossings = 1

       def output(self, t, u):
           return 1.0 if u[0][0] > 0.0 else -1.0

       def zero_crossing(self, t, u):
           return [u[0][0]]        # sign changes exactly where the output jumps

Without this the output still switches, but somewhere inside a step rather
than at a definite time, and the error controller will fight it.

Rules worth knowing
===================

.. warning::

   ``output()`` must have no side effects. A variable-step solver evaluates it
   several times per accepted step, at trial states it may throw away. Put
   state changes in ``update()``.

.. warning::

   Set ``direct_feedthrough = False`` whenever ``output()`` ignores its
   inputs. That is what lets the block sit inside a feedback loop without
   creating an algebraic loop.

* Exceptions stop the run and are reported with the full Python traceback,
  naming the block and the method that failed.
* ``print()`` goes to the Console dock.
* The :guilabel:`Init script` (:menuselection:`Simulation --> Settings`) runs
  once before each run; use it for constants and helpers shared by several
  blocks.
* A script may import a base class from ``simupy`` without becoming
  ambiguous — only classes the script itself defines are candidates.

Hardware helpers
================

``simupy`` also exposes ``require()``, ``HardwareBlock`` and ``ArduinoBoard``
for blocks that talk to something outside the process. See
:doc:`../guide/hardware`.
