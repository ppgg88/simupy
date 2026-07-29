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
       replaces the old.
   * - Double-click a block
     - A Scope opens its plot, a Python block its editor, a Subsystem its
       contents, anything else focuses the property panel.
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
   * - :kbd:`F2`
     - Names the signal the selected wire carries.

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
