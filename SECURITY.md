# Security Policy

## Supported versions

SimuPy is before 1.0. Only the latest release receives fixes.

| Version | Supported |
|---|---|
| Latest release | Yes |
| Anything older | No — please update first |

## Reporting a vulnerability

Use GitHub's private reporting: **Security → Report a vulnerability** on
https://github.com/ppgg88/simupy. That opens a private thread with the
maintainer and is the preferred route.

If that is unavailable to you, write to **paul.giroux87@gmail.com** with
`SECURITY` in the subject.

Please include what you need to reproduce it: the version, the platform, and a
model or library file if one is involved. You will get an acknowledgement, and
an honest answer about whether it is in scope — see below, because for this
project that question has an unusual answer.

Please do not open a public issue for something you believe is exploitable.

## What is *not* a vulnerability here

**Running a model runs its Python, unsandboxed, with your privileges. This is
the product, not a flaw.**

A `.spy` file is JSON and reads like data, but it carries code: block sources,
an init script, and mask parameter expressions. Pressing Run, or
`simupy-cli run`, executes all of it in the embedded interpreter with the full
builtins available. It can import `os`, open sockets, and read and write your
files. There is no sandbox, and adding one would defeat the point of a tool
whose custom blocks are written in Python.

So the following are working as intended and will be closed as such:

* arbitrary code execution from opening **and running** a model you did not
  write;
* a mask parameter expression — a field meant to hold `2*pi/T` — executing
  `__import__('os').system(...)`;
* `Ctrl+Shift+G` on a masked subsystem evaluating its expressions, which is the
  one editing action that runs model-supplied code without a run;
* the `ToFile` block writing to any path the process can reach, its path being
  an ordinary block parameter;
* the absence of a sandbox, a permission prompt on every run, or restricted
  builtins.

Treat a model from someone else exactly as you would treat a `.py` script from
the same place. `docs/limitations.rst` has a *Trust* section setting out
precisely what runs when, and the application says so in the console the first
time it opens a model carrying Python.

## What *is* in scope

Anything that breaks the boundary above, or that goes wrong before you have
decided to run anything:

* **Code running at a point the documentation says it does not.** Opening a
  model is supposed to execute nothing. If it does, that is a vulnerability and
  a serious one.
* **Memory-safety bugs reachable from a file** — a crafted `.spy` or `.spylib`
  causing a read or write out of bounds, a use-after-free, or a crash before
  any code was knowingly run.
* **Writing outside the intended directory** when installing a block library or
  a Python package: path traversal, a symlink followed where it should not be.
* **Defeating the trusted-file mechanism** — for instance trust surviving a
  change to a model's code, so a notice that should reappear does not.
* **Weaknesses in how the Python package installer fetches or verifies what it
  downloads**, or in where it puts it.
* Anything that lets a model reach outside what running a Python script of your
  own would already reach.

## What SimuPy does with the network

Two things, both only when you ask:

* the Python package installer downloads pip's official zipapp from
  `https://bootstrap.pypa.io/pip/pip.pyz` on first use, then packages from
  PyPI, into a directory under your own data folder;
* the UDP and ROS 2 hardware blocks talk to whatever a model points them at.

Nothing is fetched at startup, and nothing is sent anywhere on its own.
