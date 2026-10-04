#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <functional>
#include <chrono>
#include <sstream>

namespace test_framework {

struct TestCase {
    std::string name;
    std::function<void()> func;
};

inline std::vector<TestCase>& getRegistry() {
    static std::vector<TestCase> registry;
    return registry;
}

struct AutoReg {
    AutoReg(const std::string& name, std::function<void()> func) {
        getRegistry().push_back({name, func});
    }
};

inline int& getFailures() {
    static int failures = 0;
    return failures;
}

inline int& getAssertions() {
    static int assertions = 0;
    return assertions;
}

inline void reportFailure(const char* file, int line, const std::string& expr, const std::string& msg = "") {
    getFailures()++;
    std::cerr << "  \033[1;31m[FAILED]\033[0m " << file << ":" << line << " -> (" << expr << ")";
    if (!msg.empty()) std::cerr << " : " << msg;
    std::cerr << "\n";
}

} // namespace test_framework

#define TEST_CASE(name) \
    static void test_func_##name(); \
    static test_framework::AutoReg reg_##name(#name, test_func_##name); \
    static void test_func_##name()

#define REQUIRE(expr) do { \
    test_framework::getAssertions()++; \
    if (!(expr)) { \
        test_framework::reportFailure(__FILE__, __LINE__, #expr); \
        return; \
    } \
} while (0)

#define CHECK(expr) do { \
    test_framework::getAssertions()++; \
    if (!(expr)) { \
        test_framework::reportFailure(__FILE__, __LINE__, #expr); \
    } \
} while (0)

#define CHECK_EQ(a, b) do { \
    test_framework::getAssertions()++; \
    if (!((a) == (b))) { \
        std::ostringstream _oss; \
        _oss << (a) << " != " << (b); \
        test_framework::reportFailure(__FILE__, __LINE__, #a " == " #b, _oss.str()); \
    } \
} while (0)
