=======
Signals
=======

Naming a signal
===============

Double-click a wire, or select it and press :kbd:`F2`. The name labels every
branch of that signal on the canvas, and follows it into the scope legend and
the CSV header — so a plot says ``tracking error`` instead of ``Scope : in``,
which is what the default gives you.

.. important::

   The name belongs to the **output port that produces the signal**, not to
   the wire you happened to click. Every wire leaving one port carries the
   same thing, so naming one branch names them all; naming them separately
   would let one signal answer to two names.

Clearing the name puts the default back.

Wireless links
==============

A signal that has to cross the diagram — a measurement fed back to three
places, a mode flag read everywhere — costs more in wire than it is worth.

Drop a :guilabel:`Routing → Goto` on the source and a :guilabel:`Routing →
From` wherever you need it, give them the same tag, and the signal arrives
without a line.

Wireless links cost nothing at run time. Like Inport and Outport they are
splice points: the flattener joins whatever drives the Goto straight to
whatever the From feeds, and neither block reaches the compiled model. A loop
closed through a tag is found and solved exactly like a wired one, and a
vector signal passes through unchanged.

Scope
-----

A Goto's scope decides who can read it:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Scope
     - Reachable from
   * - ``local`` *(default)*
     - The diagram that contains it, and nothing else.
   * - ``global``
     - Anywhere in the model, across subsystem boundaries.

A From matches a local Goto in its own diagram first, then any global one.

.. important::

   That order is what lets two copies of the same subsystem both use tag
   ``err`` without stealing each other's signal — which matters as soon as a
   wireless link lives inside a library block. Keep tags local unless you mean
   otherwise.

Ambiguity is refused rather than guessed at. Each of these is reported by name
before the run starts:

* two Gotos with one tag in the same diagram,
* two global Gotos with one tag,
* a From whose tag nothing carries,
* a chain of links that closes on itself.

.. warning::

   A wireless link is invisible on the canvas. That is the point, and it is
   also the cost: a diagram wired entirely through tags is harder to read than
   one with lines. Use them for the few signals that genuinely cross the whole
   diagram, not as a default.
