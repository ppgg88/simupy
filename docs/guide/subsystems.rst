==========
Subsystems
==========

Drop a :guilabel:`Ports → Subsystem` block, open it, and build inside. An
**Inport** becomes an input port on the enclosing block and an **Outport** an
output; the number on each says which port it is, so ports keep their wiring
when you add or reorder them.

Grouping what is already drawn
==============================

The other direction is usually the useful one: select the blocks that belong
together and press :kbd:`Ctrl+G` — :menuselection:`Edit --> Group into
Subsystem`, also on the right-click menu. They move inside a new Subsystem
that takes their place on the canvas, and every signal that crossed the
selection boundary becomes a port:

* a signal coming in from outside becomes an **Inport**, wired to everything
  inside it fed — one port per signal, not per wire;
* a signal leaving becomes an **Outport**, and every destination outside is
  reconnected to that port of the new block.

Ports are numbered top to bottom, following the blocks they belong to, and
take the name of the signal when the wire had one. Nothing else changes: the
grouped model compiles and simulates to the same numbers as before.

Inport and Outport blocks cannot themselves be grouped — they define the
interface of the diagram they sit in, and moving one a level deeper would
quietly remove a port from the enclosing block. Selecting one is refused, and
the diagram is left untouched.

Breaking one open
=================

:kbd:`Ctrl+Shift+G` — :menuselection:`Edit --> Break Subsystem Open`, also on
the right-click menu of a selected Subsystem — does the reverse. Its contents
move up into the diagram around it, the Inport and Outport blocks are dropped,
and the wires that met at them are joined straight through: whatever fed a
port now feeds what the port fed, and the subsystem block itself disappears.

A block coming up whose name or identifier is already taken up there is given
a fresh one, so nothing is silently overwritten.

.. note::

   A **masked** subsystem — a library block is one — carries its parameters in
   a scope of its own, and that scope goes away with it. Its parameters are
   worked out at the moment it is broken open and written into the blocks as
   plain values, so a gain reading ``[k]`` from the mask becomes the number
   ``k`` stood for. The behaviour is preserved; the link to the library is
   not, which is the point of breaking it open.

Subsystems are structure, not behaviour
=======================================

They are expanded before the model is compiled, so a block inside one
integrates, samples and participates in feedback exactly as it would at the
top level — **nesting costs nothing at run time**.

Logged signals are named by their path, ``PI Controller/Integral``, which keeps
two copies of the same subsystem apart.

.. note::

   Because they are expanded away, subsystems do not create a scope of their
   own: there is no enabled or triggered execution, and no atomic-subsystem
   semantics. See :doc:`../limitations`.

Making one reusable
===================

A subsystem saved into a library becomes a palette block with its own
parameters — see :doc:`libraries`. That is where a subsystem stops being a
tidy-up and starts being a component.
