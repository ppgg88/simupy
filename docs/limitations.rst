===========
Limitations
===========

Worth knowing before you rely on it.

Simulation
==========

Python blocks are serialised by the GIL
   Several of them in one model run one at a time. A single in-process
   interpreter also means a segfault in a native extension takes the
   application with it. This is inherent to the embedded design and would need
   an out-of-process worker to change.

Algebraic loops are solved numerically, so they cost
   Each loop runs a Newton solve on every output evaluation, and the Jacobian
   is rebuilt by finite differences each iteration. A loop is still worth
   breaking with a Unit Delay when you can.

Zero-crossing detection needs the block to declare it
   The built-in discontinuous blocks do, and a Python block can via
   ``zero_crossings`` and ``zero_crossing()``. A block that switches without
   declaring anything is still crossed blindly.

Two events inside one step are found one at a time
   The step lands on the earliest crossing and the next step finds the rest,
   which is right but can be slow if a model chatters between two surfaces. A
   model whose crossings collapse onto each other while the clock stands still
   is stopped and named rather than left to crawl, but the cure is yours: give
   the comparison some hysteresis, or a dead band.

SDIRK2 is only second order
   It earns its keep on stiff models; on a well-conditioned one at a tight
   tolerance the explicit method is cheaper. There is no automatic stiffness
   detection, so the choice is yours.

``Derivative`` is a plain finite difference
   First order, and it amplifies noise. Prefer a filtered derivative,
   ``s/(tau*s + 1)`` as a ``TransferFcn``, where accuracy matters.

Modelling
=========

Subsystems are virtual
   They organise a diagram but do not create a scope of their own: there is no
   enabled or triggered execution, and no atomic-subsystem semantics.

Mask expressions overwrite the typed value
   Opening a library block and editing a bound parameter by hand does nothing:
   the next run computes it again from the mask. The property panel shows one
   or the other, never both, so the trap is visible rather than silent.

Library blocks are copies, not links
   Dropping one stamps out the saved diagram; editing the library afterwards
   leaves existing instances alone. That is what keeps a |spy| self-contained
   and reproducible, and it is the opposite of what a Simulink library link
   does. Re-drop the block to take a new version.

Mask expressions need Python
   They are evaluated by the embedded interpreter, so a masked block in a
   build where Python failed to start reports the failure rather than falling
   back to its typed values.

Editor
======

Wire routing is greedy
   Wires are laid one after another, each avoiding the ones already placed, so
   the result is good but not globally optimal — a different order could
   occasionally do better. There are also no user-editable waypoints: the
   route is entirely the router's choice.

A wireless link is invisible on the canvas
   That is the point, and it is also the cost. There is no "find every From
   reading this tag" command.

Interaction
===========

A control change lands at the next evaluation
   With a variable-step solver that means somewhere inside the step in
   progress rather than at a declared instant. Invisible for a slider; for a
   toggle or a button it is a discontinuity crossed blind, so cap the maximum
   step if the switching instant matters.

Trust
=====

.. warning::

   Running a model runs its Python, unsandboxed, with your privileges. A
   |spy| file is not the inert data file its JSON appearance suggests. Treat
   one that arrives from elsewhere exactly as you would treat a ``.py`` script
   from the same place.

Running a model executes everything in it
   Pressing :kbd:`F5`, or ``simupy-cli run``, compiles and runs the model's
   init script, every Python block's source, every Python block's parameters
   script, and every mask parameter expression. All of it runs in the embedded
   interpreter with the full builtins available, so any of it can import
   :py:mod:`os`, open sockets, or read and write your files. There is no
   sandbox, and adding one would defeat the point of the product.

   A mask expression is the part worth naming, because it does not look like
   code: a field meant to hold ``2*pi/T`` will just as happily hold
   ``__import__('os').system(...)``.

Opening a model does not run anything
   Loading a |spy| deserializes it and no more. What it does do is add the
   file's own directory to :py:data:`sys.path`, for the rest of the session
   and for every model opened after it — so a module dropped beside a |spy|
   becomes importable by anything that later runs.

Breaking a masked subsystem open evaluates its expressions
   :kbd:`Ctrl+Shift+G` has to resolve the mask to hand its values down, which
   means running those expressions. It is the one editing action that executes
   model-supplied code without a run.

``ToFile`` writes wherever it is pointed
   The path is an ordinary block parameter, taken as given, so a model can
   write anywhere the process can. This is a plain consequence of the block
   existing, not a defect in it.

Hardware
========

The Arduino firmware is untested on hardware
   Only the host side is covered by tests, against a board simulated on a
   pseudo-terminal. That the sketch implements the protocol identically is the
   part to confirm at the first connection.

The ROS 2 block is a starting point
   It publishes ``std_msgs/Float64MultiArray`` and the shape of the block is
   right, but the message type is meant to be changed for your graph, and it
   has not been exercised against a running ROS 2 installation.
