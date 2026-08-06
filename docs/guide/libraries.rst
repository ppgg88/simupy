================
Block libraries
================

Saving a block
==============

A subsystem you would rather not rebuild, or a Python block you want in every
model, becomes a palette block: select it and press :kbd:`Ctrl+Shift+B`
(:menuselection:`Library --> Save Selection as Block`). Give it a name, a
category, an icon, and the parameters it should expose.

It appears in the palette immediately and stays there across restarts.

Mask parameters
===============

Parameters are what make a saved subsystem worth saving. A block that
hard-codes its gains is a copy; one that exposes them is a component.

Declare them in the dialog's :guilabel:`Parameters` tab, then open the block
and bind each inner parameter to one with the |fx| button beside it. The
binding is a Python expression evaluated when the model is flattened, so all
of these work:

.. code-block:: python

   kp                        # a parameter straight through
   [kp]                      # a vector-valued Gain
   [1, 2*zeta*wn, wn**2]     # a denominator built from two parameters
   np.diag(q)                # anything NumPy can produce

.. |fx| replace:: :guilabel:`ƒx`

The names in scope are the block's own parameters, and nothing else. Bindings
nest: a custom block placed inside another can pass a parameter down. A plain
:guilabel:`Ports → Subsystem` declares no parameters, so no |fx| buttons
appear inside it.

Python blocks get the same treatment with less work — a declared parameter
arrives as ``self.params["name"]``, so a script that already reads
``self.params`` needs no change. Mask values override the free-form
:guilabel:`Parameters` field, which becomes the place to put defaults.

.. warning::

   The names ``code``, ``className`` and ``parameters`` are the Python block's
   own and cannot be used for a mask parameter.

.. warning::

   A mask expression is evaluated at flatten time and **overwrites the typed
   value**. Opening a library block and editing a bound parameter by hand does
   nothing: the next run computes it again from the mask. The property panel
   shows one or the other, never both, so the trap is visible rather than
   silent.

Sharing
=======

Blocks live in libraries, and a library is one |spylib| file: metadata, every
saved diagram, every Python source, and the icons base64-encoded inline.
Nothing points outside it, so sharing a library is sending the file.

The recipient drops it in ``~/.local/share/simupy/libraries/`` — or
``%APPDATA%\SimuPy\libraries`` on Windows — or uses :menuselection:`Library -->
Manage Libraries --> Install`.

``SIMUPY_LIBRARY_PATH`` takes a colon-separated list of extra directories,
which is how a library is kept in a project repository rather than in a home
directory. Directories are scanned in order and the user folder is scanned
last, so a local copy wins over one installed system-wide.

Models stay self-contained
==========================

Saving writes each custom block's full contents and Python source into the
|spy|, and notes which library it came from.

A model that reaches a machine without that library **still opens and still
simulates exactly the same numbers**. It loses the icon, the parameter labels
and the palette entry, and the console says so. Installing the library
afterwards silently restores all three, because the block remembers the type
name it wants even while standing in as a plain subsystem.

.. note::

   Library blocks are copies, not links. Dropping one stamps out the saved
   diagram; editing the library afterwards leaves existing instances alone.
   That is what keeps a |spy| self-contained and reproducible, and it is the
   opposite of what a Simulink library link does. Re-drop the block to take a
   new version.

Python packages
===============

A library whose blocks ``import scipy`` is not self-contained the way one made
of subsystems is: the file carries the code, not the package it needs. So a
library can say what it needs, and SimuPy can fetch it.

Declare them, and :menuselection:`Library --> Manage Libraries` shows each one
against the interpreter actually running — installed and at what version, or
missing and under what pip name. :guilabel:`Install packages` lights up when
something is missing.

Where they go
-------------

Into SimuPy's own folder, ``~/.local/share/simupy/python-packages``, which is
put on ``sys.path`` ahead of everything else at startup. Never into the
system's Python:

* a distribution's ``site-packages`` is not an application's to write to, and
  on Debian and Ubuntu pip refuses outright (:pep:`668`);
* in a Flatpak, ``/app`` is read-only, while the user's data folder is not;
* the Windows build ships its own interpreter and no pip at all.

A folder of our own is the one place writable in all three. The installer
itself is pip's official zipapp, fetched once on first use — so nothing has to
be installed before SimuPy can install anything, and the system's Python is
never touched. Installing needs a network connection.

Declaring them
--------------

By module name, optionally with the pip name when it differs and a word on what
it is for::

    serial   → pyserial   talking to an Arduino

The block editor reads your source and offers what it finds — ``import x``,
``from x import y``, and the module named in ``require("x", "pkg")``, which is
also where the pip name and the reason come from. It skips the standard library
and what SimuPy already ships. Treat it as a prompt, not the record: a static
read cannot see an import assembled at runtime.

.. note::

   A package installed while SimuPy is running is only picked up if nothing has
   tried to import it yet. Restart after installing if a block still cannot
   find it.

Icons
=====

An imported SVG or PNG/JPEG, embedded in the library file, or a short text
label drawn centred the way ``Gain`` draws its value.

SVG needs Qt SVG, which is a separate package on most distributions. Without
it the build succeeds and raster icons still work.
