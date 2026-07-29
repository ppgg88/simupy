==========
The engine
==========

How a model becomes a run
=========================

.. code-block:: text

   Model            blocks, wires, layout, solver settings — pure data,
     │              nested one level per subsystem
     ▼
   FlatModel        every subsystem expanded, Inport/Outport wiring spliced
     │              through, Goto/From spliced too, blocks addressed by index
     ▼
   Scheduler        propagates signal widths to a fixpoint, assigns state and
     │              zero-crossing slices, then decomposes the direct-feedthrough
     │              graph into strongly connected components
     ▼
   CompiledModel    execution plan + wiring resolved to signal indices
     │
   Simulator        owns every buffer; one accepted solver step is one major
     │              step and one logged sample
     ├── OdeSolver          Dormand-Prince 5(4) / SDIRK2 / RK4 / Heun / Euler
     ├── loop solver        Newton on the torn signals, numerical Jacobian
     ├── event location     bisection onto each zero crossing
     ├── RealTimePacer      absolute deadlines against the wall clock
     └── Block::compute*    native C++ blocks, or PythonBlock
                                 │
                            PythonEngine — one embedded interpreter, GIL taken
                            once per run and handed back while pacing waits

Decisions worth calling out
===========================

Subsystems are structure, not behaviour
---------------------------------------

They are expanded before anything else runs, so the solver, the scheduler and
the loop solver only ever see one flat diagram. Nothing below the flattening
step knows hierarchy exists.

A custom block is a registry entry
----------------------------------

Loading a library adds a ``BlockType`` whose factory stamps out a Subsystem
pre-filled with the saved diagram, or a Python block pre-filled with the saved
source. Everything downstream — the palette, the property panel, the
serializer, the scheduler — sees an ordinary block type and needs no idea that
libraries exist.

Masks resolve in the same pass that flattens: each block's expressions are
evaluated against the parameters of the subsystem enclosing it, top-down, so a
subsystem's freshly resolved values become the scope for its contents and a
parameter can be threaded down through nested custom blocks. By the time the
scheduler runs, every parameter is a plain value.

Width propagation runs to a fixpoint
------------------------------------

Rather than in one topological pass. A block sees an unresolved input as
unconnected, sizes its outputs provisionally, and is revisited until nothing
changes — which settles even when the width is determined through a feedback
loop.

.. note::

   A consequence worth knowing when writing a block that owns a resource:
   ``setup()`` is called several times per compile, on a fresh instance each
   time, and only the last survives. See :doc:`../guide/hardware`.

Algebraic loops are solved, not refused
---------------------------------------

Tarjan's algorithm finds each cycle, the edges that close it are torn, and
Newton iterates until re-evaluating the group reproduces the torn values. A
loop with no unique solution — unity positive feedback around a gain of one —
is reported by name, because there the matrix :math:`J - I` really is singular
rather than merely awkward.

Events are located, not stepped over
------------------------------------

A block with a kink declares zero-crossing functions; when one changes sign
across an accepted step, the step is bisected until it lands on the crossing.

Two details make it work:

#. The trial steps are forced through the error controller, which would
   otherwise refuse exactly the trials that straddle the discontinuity.
#. The state kept is the one from *before* the event, since a Runge-Kutta step
   ending on the crossing evaluates its last stages on the far side.

Each crossing is latched and disarmed once handled, so a state that comes to
rest on its own switching surface cannot retrigger forever.

A signal's name lives on the port that produces it
--------------------------------------------------

Not on the wire, and not on the sink that logs it: a fan-out is one signal
drawn several times, and a wireless link is the same signal with no wire at
all. Naming the producer is the only place that stays true through both, which
is why the label survives a branch, a Goto/From hop, and a subsystem boundary
without any special case.

Wires avoid blocks, and each other
----------------------------------

The route is an A* search on a sparse grid built from the endpoints and the
obstacle edges. Blocks are hard obstacles, including their name and port
arrows — a wire drawn across a label is as unreadable as one drawn across a
block.

Other wires are costs rather than walls: sharing a lane is charged per unit of
shared length, because two wires on top of each other cannot be told apart,
while a right-angle crossing costs a small flat amount, below the price of a
bend, so the router prefers a clean crossing to an extra corner taken to dodge
it. Wires carrying the same signal are exempt from both, which is what lets a
fan-out read as one branching line.

Logging is tied to accepted solver steps
----------------------------------------

That is what makes a variable-step plot dense through a fast transient and
sparse through a quiet stretch. The log decimates itself once it hits the
sample cap, halving in place, so a long run stays within bounded memory
without losing its shape.

The GIL is taken once for the whole run
---------------------------------------

Acquiring it per block evaluation would cost more than most blocks do. It is
handed back while a real-time run waits between steps, which is what lets a
hardware block's background thread do its I/O.
