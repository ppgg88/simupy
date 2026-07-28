#pragma once

#include <string>

namespace harness {

void check(bool condition, const std::string& what);
void checkClose(double actual, double expected, double tolerance,
                const std::string& what);
void beginTest(const std::string& name);

void runTest(void (*test)());

int summary();

}

using harness::beginTest;
using harness::check;
using harness::checkClose;
using harness::runTest;
