# Contributing to SimuPy

Thank you for taking the time. This file is what a contributor needs to know
before opening a pull request: how to build, what has to pass, and the
conventions the existing code follows.

By taking part you agree to the [Code of Conduct](CODE_OF_CONDUCT.md).

## Building

```sh
sudo apt install cmake g++ qt6-base-dev qt6-charts-dev qt6-svg-dev \
                 libeigen3-dev nlohmann-json3-dev python3-dev \
                 python3-pybind11 python3-numpy

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Options worth knowing:

| Option | Effect |
|---|---|
| `-DSIMUPY_BUILD_GUI=OFF` | Engine, CLI and tests only. CI builds this to prove the engine still compiles without Qt. |
| `-DSIMUPY_BUILD_TESTS=OFF` | Skips the test suite. |
| `-DSIMUPY_ENABLE_ASAN=ON` | Address and undefined-behaviour sanitizers. |

## Testing

```sh
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

`simupy-tests` covers the engine, the file formats and the Python bridge.
`simupy-gui-tests` drives the real window offscreen with synthesised key and
mouse events. Both must pass before a pull request is ready.

Under the sanitizers, as the CI job does:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSIMUPY_ENABLE_ASAN=ON
cmake --build build-asan -j$(nproc)

ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 \
  QT_QPA_PLATFORM=offscreen ctest --test-dir build-asan --output-on-failure
```

`detect_leaks=0` because the embedded interpreter never gives its memory back.
UBSan does not fail the process on a finding, so read the log rather than
trusting the exit code — the CI job greps it for you.

## What CI runs

Six jobs, all of which must be green:

| Job | What it proves |
|---|---|
| **Build and test** | Linux, Release, both suites. |
| **Sanitizers** | The suite again under ASan and UBSan. |
| **Build and test (Windows)** | MSVC, and the headless build. |
| **Flatpak** | The bundle builds, installs, and runs two example models. |
| **Documentation** | Sphinx with `-W`, so a warning is an error. |
| **Generated files are up to date** | Re-runs the example and library generators and diffs the tree; validates the workflows and the AppStream metadata. |

If you change `examples/make_examples.py` or `examples/make_libraries.py`, run
them and commit the result, or that last job will fail.

## Tests are part of the change

A fix without a test that fails before it is not finished. The habit in this
repository is to check that the test genuinely catches the bug — write it,
watch it fail against the unfixed code, then fix. Several tests here carry a
comment saying what used to go wrong, which is the most useful thing a
regression test can record.

Prefer a test that pins the behaviour a user would notice over one that pins
the implementation.

## Style

There is no `.clang-format` yet, so match the surrounding code:

* four spaces, never tabs;
* 80 columns as the target, broken only where breaking would hurt more;
* `lowerCamelCase` for functions and variables, `UpperCamelCase` for types,
  trailing underscore on private members;
* comments explain *why*, and only where the code cannot. A comment restating
  the line above it is noise; one naming a language pitfall, an API contract or
  a measured number is worth keeping. Keep them short.

## Commit messages

One line, in the imperative, prefixed with the kind of change:

```
[fix.] refuse a non-finite solver step instead of writing a NaN into the state
[feat.] add undo and redo, by snapshotting the model on every edit
[perf.] keep the SDIRK2 Jacobian across discrete updates
[doc.] state the trust boundary
[test.] cover the run controller directly
[refactor.] drop Block::reset
[ci.] run the suite under the sanitizers
```

Say what changed and why it is better, not which files you touched. If the
reasoning is long, put it in the body — that is what a body is for, and it
saves a comment in the code.

## Pull requests

* One concern per pull request. A fix and a refactor in the same diff are hard
  to review and harder to revert.
* Say how you verified it, not just that you did.
* Update the documentation in the same change. `docs/` is built with `-W`, so a
  stale cross-reference fails CI.
* Say plainly in the description what a user would notice, so it can be lifted
  into the release notes.

## Security

Do not open a public issue for a vulnerability. See [SECURITY.md](SECURITY.md),
which also sets out what is *not* a vulnerability in this project — running a
model runs its Python by design.

## Licence

SimuPy is GPL-3.0. Contributions are accepted under the same licence.
