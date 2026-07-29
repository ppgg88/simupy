============
Command line
============

``simupy-cli``
==============

.. code-block:: console

   $ simupy-cli run <model.spy> [options]   # simulate a model
   $ simupy-cli blocks                      # every block type and its parameters
   $ simupy-cli info <model.spy>            # blocks, wires, layout
   $ simupy-cli libraries                   # installed block libraries

Options for ``run``
-------------------

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Option
     - Effect
   * - ``--stop <t>``
     - Override the stop time.
   * - ``--step <h>``
     - Override the fixed step size.
   * - ``--solver <name>``
     - ``rk45``, ``sdirk2``, ``rk4``, ``heun``, ``euler``, ``discrete``.
   * - ``--csv <path>``
     - Write all logged signals to a CSV file.
   * - ``--realtime [factor]``
     - Pace against the wall clock; the optional factor is simulated seconds
       per real second, default 1.
   * - ``--unbounded``
     - Run until interrupted with :kbd:`Ctrl+C` rather than to a stop time.
   * - ``--quiet``
     - Only report errors.

:kbd:`Ctrl+C` is a clean stop: the run ends at the next major step, and the
summary and the CSV are still written.

Examples
--------

.. code-block:: console

   $ simupy-cli run model.spy --stop 20 --csv out.csv
   $ simupy-cli run model.spy --realtime 2
   $ simupy-cli run model.spy --unbounded --csv trace.csv

``simupy``
==========

The desktop application. Positional argument is a model to open.

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Option
     - Effect
   * - ``--run``
     - Simulate the model as soon as it opens.
   * - ``--light``
     - Use the light theme.

Environment
===========

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Variable
     - Meaning
   * - ``SIMUPY_LIBRARY_PATH``
     - Colon-separated extra directories scanned for |spylib| files, before
       the per-user folder.
