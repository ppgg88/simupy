==========
The editor
==========

Placing and wiring
==================

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Gesture
     - Effect
   * - Type over the canvas
     - Searches every block type and drops the one you pick at the cursor.
       :kbd:`Ctrl+Space` from the menu, :kbd:`Esc` abandons.
   * - Drag from the library
     - Places a block where you drop it. Double-clicking the library entry
       drops it in the middle of the view.
   * - Drag port to port
     - Wires them, in either direction. An input takes one wire; a new one
       replaces the old. Dropping is forgiving: releasing anywhere in the band
       beside a port connects to it, and on a block with several ports the
       bands meet halfway between them.
   * - Double-click a block
     - A Scope opens its plot, a Python block its editor, a Subsystem its
       contents. Anything else opens a window listing what each port is wired
       to and holding the block's parameters; :guilabel:`OK` closes it. Edits
       take effect as they are made, and the Properties dock stays in step.
   * - :kbd:`Ctrl+M`
     - Mirrors the selection — how a feedback path is drawn right to left.

Wires show their signal width once a run has compiled the model, so a vector
signal is visible at a glance.

Selection and the clipboard
===========================

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Shortcut
     - Effect
   * - :kbd:`Ctrl+C`, :kbd:`Ctrl+X`, :kbd:`Ctrl+V`
     - Copy, cut, paste. A paste lands at the cursor and comes out selected.
   * - :kbd:`Ctrl+D`
     - Duplicates the selection without touching the clipboard.
   * - :kbd:`Delete`, :kbd:`Backspace`
     - Removes the selected blocks and wires.
   * - :kbd:`Ctrl+A`
     - Selects everything on the current diagram.
   * - :kbd:`Ctrl+Z`, :kbd:`Ctrl+Shift+Z`
     - Undo and redo, a hundred steps deep. Everything that changes the model
       is covered: deleting, grouping, pasting, a parameter typed into the
       Properties dock, the solver settings. Each step restores the model
       whole, so it returns you to the top of the diagram and then back down
       into whichever subsystem you had open. Neither is offered while a run is
       in flight, and a run's results are not part of the history.
   * - :kbd:`F2`
     - Names the signal the selected wire carries.
   * - :kbd:`Ctrl+G`
     - Groups the selection into a Subsystem, wired as it was. See
       :doc:`subsystems`.
   * - :kbd:`Ctrl+Shift+G`
     - Breaks the selected Subsystem open, moving its contents up into this
       diagram.

What travels with a copy: the blocks, their parameters, their signal names and
mask bindings, everything nested inside a Subsystem, and the wires running
between two selected blocks. A wire with one end outside the selection is
dropped — it would have nothing to reconnect to.

Pasted blocks get fresh identifiers and, where a name is taken, a fresh name,
so pasting back into the diagram you copied from is as safe as pasting into
another one.

.. note::

   A selection goes on the clipboard as JSON, under a private MIME type
   **and** as plain text. A copy can therefore be pasted into a second running
   copy of SimuPy, kept in a scratch file, or read in a diff.

Navigating
==========

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Shortcut
     - Effect
   * - :kbd:`Ctrl+Down`, :kbd:`Ctrl+Up`
     - Enters the selected subsystem, or leaves it. The breadcrumb above the
       canvas shows where you are and jumps back a level.
   * - :kbd:`Shift+drag`, middle button
     - Pans.
   * - :kbd:`Ctrl+wheel`
     - Zooms.
   * - :kbd:`Ctrl+9`
     - Fits the whole diagram in the window.
   * - :kbd:`Ctrl+0`
     - Actual size.

Saving
======

:kbd:`Ctrl+S` saves, :kbd:`Ctrl+Shift+S` saves under a new name. The title bar
carries a ``*`` while the model has edits that are not on disk yet.

:menuselection:`File --> Autosave every 30 seconds` writes the model back to
its own file every 30 seconds, but only while there is something to write. It
stays out of the way in three cases: a model that has never been saved is left
alone rather than raising a :guilabel:`Save As` dialog, a run in flight is
allowed to finish its tick first, and a write that fails is reported in the
Console instead of a dialog every half minute — the edits stay in memory and
the ``*`` stays up. The setting is remembered between sessions.

Running
=======

:kbd:`F5` runs, :kbd:`Shift+F5` stops. The toolbar carries the same two, with
a green triangle and a red square, and greys out whichever does not apply.

While a run is in flight the diagram is **locked, not disabled**: it still
pans, zooms, selects and takes control input, but nothing can be moved, wired,
deleted or pasted. See :doc:`controls`.

Plots
=====

A plot opens as its own window. Two buttons decide where it lives:

:guilabel:`On top`
   Pins it above the main window, so it stays visible while you work on the
   diagram.

:guilabel:`Dock`
   Folds it into a tab beside the Console.

Each plot decides for itself, and either can also be dragged into place by its
title bar.

:guilabel:`Window` sets how many seconds of history stay on screen. At
:guilabel:`Whole run` the axis grows with the run, as it always did; at any
other value the time axis **holds that width** and the trace scrolls through
it, which is what makes a long or unbounded run readable — the last few seconds
stay the same size instead of being squeezed into an ever-wider axis. The
vertical auto-scale then follows the visible stretch alone, so an early spike
no longer flattens what is happening now.

The setting belongs to the Scope block, under :guilabel:`Time window (s)`, so
it is saved with the model and can also be typed in the block's own window.
