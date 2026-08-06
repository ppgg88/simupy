============
File formats
============

Both are plain JSON: readable in a diff, editable by hand, and sensible to
keep under version control.

.. warning::

   Being JSON does not make them data. A model carries Python — block sources,
   an init script, mask expressions — and running it runs all of that, with
   your privileges and no sandbox. See :doc:`../limitations` for what runs
   when.

Models — |spy|
==============

.. code-block:: json

   {
     "format": "simupy-model",
     "version": 1,
     "name": "pid control loop",
     "solver": { "method": "rk45", "stopTime": 8.0, "relTol": 1e-6 },
     "blocks": [
       { "id": "plant", "type": "TransferFcn", "name": "Plant",
         "geometry": { "x": 20, "y": 0, "width": 130, "height": 60 },
         "params": { "numerator": [2.0], "denominator": [1.0, 3.0, 2.0] },
         "signals": { "0": "plant output" } }
     ],
     "connections": [
       { "id": "wire_1",
         "source": { "block": "pid", "port": 0 },
         "target": { "block": "plant", "port": 0 } }
     ]
   }

Block fields
------------

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Field
     - Meaning
   * - ``id``
     - Unique within its diagram. Wires refer to it.
   * - ``type``
     - The registered block type. A type this installation does not have
       degrades to the nearest built-in that can carry its data.
   * - ``params``
     - The JSON type carries the parameter type, so a model round-trips
       without consulting the registry.
   * - ``expressions``
     - Mask bindings: ``{"gain": "[kp]"}``. See below.
   * - ``signals``
     - Signal names by output port: ``{"0": "error"}``.
   * - ``contents``
     - Present on a Subsystem: the same structure again, one level down.

Solver fields
-------------

``method``, ``startTime``, ``stopTime``, ``fixedStep``, ``maxStep``,
``minStep``, ``initialStep``, ``relTol``, ``absTol``, ``maxLoggedSamples``,
``realTime``, ``realTimeFactor``, ``unbounded``.

Libraries — |spylib|
====================

One document holding every saved block, each with its mask, its icon and
either a diagram or a Python source:

.. code-block:: json

   {
     "format": "simupy-library",
     "version": 1,
     "name": "Control Toolbox",
     "revision": 3,
     "python": [
       { "module": "serial", "package": "pyserial",
         "purpose": "talking to an Arduino" }
     ],
     "blocks": [
       { "name": "PIAntiWindup", "kind": "subsystem", "category": "Control",
         "icon": { "kind": "text", "text": "PI" },
         "params": [
           { "name": "kp", "label": "Proportional gain",
             "kind": "real", "default": 1.0 }
         ],
         "contents": {
           "blocks": [
             { "id": "gain", "type": "Gain", "name": "Kp",
               "params": { "gain": [1.0] },
               "expressions": { "gain": "[kp]" } }
           ],
           "connections": []
         } }
     ]
   }

``kind`` is ``subsystem`` or ``python``. Icons are base64 for SVG and raster,
plain text for a label.

``python`` lists the packages the blocks import, and is absent when they import
nothing beyond what SimuPy ships. ``package`` is the pip name, given only when
it differs from the module; ``purpose`` is shown to whoever is missing it. See
:doc:`../guide/libraries`.

.. note::

   ``expressions`` is the mask binding: ``params`` holds the last resolved
   value so the file stays readable, and ``expressions`` holds what actually
   decides it.

Generating them
===============

``examples/make_examples.py`` and ``examples/make_libraries.py`` write the
shipped models and libraries this way, and are the easiest place to look when
scripting either.

CI regenerates both and fails if the result differs from what is committed.
