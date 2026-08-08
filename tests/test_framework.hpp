#pragma once

// A deliberately tiny test framework: no external dependency, no network
// fetch, no package manager -- just a self-registering TEST() macro and a
// couple of CHECK assertions, in the same spirit as GoogleTest/Catch2 but
// scoped to exactly what this project needs. This keeps FileWarden buildable
// with nothing but a C++17 compiler and CMake, anywhere, offline.

#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace testing {

struct AssertionFailure {
    std::string message;
};

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        registry().push_back(TestCase{name, std::move(fn)});
    }
};

inline int runAll() {
    int passed = 0, failed = 0;
    for (auto& t : registry()) {
        try {
            t.fn();
            std::cout << "[ OK ] " << t.name << "\n";
            ++passed;
        } catch (const AssertionFailure& e) {
            std::cout << "[FAIL] " << t.name << " - " << e.message << "\n";
            ++failed;
        } catch (const std::exception& e) {
            std::cout << "[FAIL] " << t.name << " - unexpected exception: " << e.what() << "\n";
            ++failed;
        } catch (...) {
            std::cout << "[FAIL] " << t.name << " - unknown exception\n";
            ++failed;
        }
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}

} // namespace testing

#define FW_CONCAT_INNER(a, b) a##b
#define FW_CONCAT(a, b) FW_CONCAT_INNER(a, b)

#define TEST(name)                                                                 \
    static void FW_CONCAT(test_fn_, name)();                                       \
    static ::testing::Registrar FW_CONCAT(registrar_, name)(#name, FW_CONCAT(test_fn_, name)); \
    static void FW_CONCAT(test_fn_, name)()

#define CHECK_EQ(a, b)                                                    \
    do {                                                                  \
        if (!((a) == (b))) {                                              \
            std::ostringstream oss_;                                      \
            oss_ << "CHECK_EQ failed: " #a " != " #b " (got '" << (a)     \
                 << "', expected '" << (b) << "')";                       \
            throw ::testing::AssertionFailure{oss_.str()};                \
        }                                                                 \
    } while (0)

#define CHECK_TRUE(cond)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            throw ::testing::AssertionFailure{"CHECK_TRUE failed: " #cond}; \
        }                                                                 \
    } while (0)

#define CHECK_THROWS(expr)                                                        \
    do {                                                                          \
        bool threw_ = false;                                                      \
        try { (void)(expr); } catch (...) { threw_ = true; }                      \
        if (!threw_) {                                                            \
            throw ::testing::AssertionFailure{"CHECK_THROWS failed: " #expr " did not throw"}; \
        }                                                                         \
    } while (0)
