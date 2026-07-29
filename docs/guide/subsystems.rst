==========
Subsystems
==========

Drop a :guilabel:`Ports → Subsystem` block, open it, and build inside. An
**Inport** becomes an input port on the enclosing block and an **Outport** an
output; the number on each says which port it is, so ports keep their wiring
when you add or reorder them.

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
