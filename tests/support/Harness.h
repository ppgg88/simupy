#pragma once

#include <string>

/// A test suite with no framework: the engine has a narrow surface and the
/// checks are mostly numerical, so a handful of helpers is enough.
namespace harness {

void check(bool condition, const std::string& what);
void checkClose(double actual, double expected, double tolerance,
                const std::string& what);
void beginTest(const std::string& name);

/// Runs one test, turning an escaped exception into a failure rather than
/// letting it take the whole suite down.
void runTest(void (*test)());

/// Prints the tally and returns the process exit code.
int summary();

}  // namespace harness

using harness::beginTest;
using harness::check;
using harness::checkClose;
using harness::runTest;
