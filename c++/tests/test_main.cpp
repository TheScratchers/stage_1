#include "test_framework.hpp"
#include <iostream>
#include <iomanip>

int main() {
    std::cout << "\n========================================\n";
    std::cout << "   Stage 1: C++ Automated Unit Tests   \n";
    std::cout << "========================================\n\n";

    auto& tests = test_framework::getRegistry();
    int passed = 0;
    int failed = 0;

    for (const auto& test : tests) {
        int failuresBefore = test_framework::getFailures();
        std::cout << "  RUNNING: " << std::left << std::setw(40) << test.name << " ... ";
        std::cout.flush();

        auto t0 = std::chrono::high_resolution_clock::now();
        try {
            test.func();
        } catch (const std::exception& e) {
            test_framework::reportFailure("exception", 0, "std::exception", e.what());
        } catch (...) {
            test_framework::reportFailure("exception", 0, "unknown exception");
        }
        auto elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t0).count();

        if (test_framework::getFailures() == failuresBefore) {
            std::cout << "\033[1;32m[PASSED]\033[0m (" << std::fixed << std::setprecision(2) << elapsedMs << " ms)\n";
            passed++;
        } else {
            std::cout << "\033[1;31m[FAILED]\033[0m (" << std::fixed << std::setprecision(2) << elapsedMs << " ms)\n";
            failed++;
        }
    }

    std::cout << "\n----------------------------------------\n";
    std::cout << "Summary: " << passed << " passed, " << failed << " failed, "
              << test_framework::getAssertions() << " assertions executed across "
              << tests.size() << " test suites.\n";
    std::cout << "----------------------------------------\n\n";

    return (failed == 0) ? 0 : 1;
}
