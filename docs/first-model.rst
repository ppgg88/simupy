==================
Your first model
==================

Open ``examples/pid_control_loop.spy`` and press :kbd:`F5`. A scope opens and
plots a step response. Everything below builds the same thing from an empty
canvas.

Placing blocks
==============

Point at the canvas and **start typing**. A search opens at the cursor:

.. code-block:: text

   ste↵     a Step source
   tf↵      a Transfer Function
   sum↵     a summing junction

Matching runs on the name, the category and the description, and falls back to
a subsequence, so an abbreviation usually finds it. :kbd:`Ctrl+Space` does the
same from the menu and :kbd:`Esc` abandons.

Blocks can also be dragged from the **Block Library** panel, or dropped in the
middle of the view by double-clicking there.

Wiring
======

Drag from an output port to an input port — either direction works. An input
accepts one wire; a new one replaces the old.

To draw a feedback path right to left, select the block and press
:kbd:`Ctrl+M` to mirror it, so its input sits on the right.

A minimal loop
==============

#. A **Step** for the setpoint.
#. A **Sum** with signs ``+-``.
#. A **PID**, then a **TransferFcn** with numerator ``2`` and denominator
   ``1 3 2`` as the plant.
#. A **Scope**.
#. Wire setpoint and plant output into the Sum, the Sum into the PID, the PID
   into the plant, and the plant into the Scope.

Press :kbd:`F5`. The scope fills in as the run proceeds.

.. tip::

   Select the wire leaving the Sum and press :kbd:`F2`. Call it ``error``.
   The name now labels every branch of that signal and appears in the scope
   legend instead of ``Scope : in`` — see :doc:`guide/signals`.

Where to go next
================

.. list-table::
   :widths: 34 66

   * - :doc:`guide/editor`
     - The canvas in full: selection, clipboard, navigation, zoom.
   * - :doc:`reference/python-blocks`
     - When no built-in block does what you need.
   * - :doc:`guide/simulation`
     - Choosing a solver, running in real time, running without end.
   * - :doc:`guide/hardware`
     - Driving something outside the process.
