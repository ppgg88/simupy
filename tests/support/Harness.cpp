#include "Harness.h"

#include <cmath>
#include <iomanip>
#include <iostream>

namespace harness {
namespace {

int g_failures = 0;
int g_checks = 0;

}  // namespace

void check(bool condition, const std::string& what) {
    ++g_checks;
    if (condition) return;
    ++g_failures;
    std::cout << "  FAIL  " << what << '\n';
}

void checkClose(double actual, double expected, double tolerance,
                const std::string& what) {
    ++g_checks;
    const double error = std::abs(actual - expected);
    if (error <= tolerance) return;
    ++g_failures;
    std::cout << "  FAIL  " << what << "\n        expected "
              << std::setprecision(10) << expected << ", got " << actual
              << " (error " << error << " > " << tolerance << ")\n";
}

void beginTest(const std::string& name) {
    std::cout << "- " << name << std::endl;
}

void runTest(void (*test)()) {
    try {
        test();
    } catch (const std::exception& error) {
        ++g_failures;
        std::cout << "  FAIL  threw: " << error.what() << '\n';
    }
}

int summary() {
    std::cout << '\n'
              << (g_failures == 0 ? "PASSED" : "FAILED") << "  " << g_checks
              << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}

}  // namespace harness
