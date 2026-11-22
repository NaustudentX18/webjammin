// WeBjammin — test harness.
//
// A tiny assertion framework: no external test dependency on the device.
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace wj::test {

struct Registry {
    struct Case {
        const char* name;
        void (*fn)();
    };
    std::vector<Case> cases;
    int failures = 0;
    std::string current;
};

inline Registry& registry() {
    static Registry r;
    return r;
}

inline void fail(const char* file, int line, const std::string& msg) {
    ++registry().failures;
    std::fprintf(stderr, "  FAIL %s (%s:%d): %s\n", registry().current.c_str(), file, line,
                 msg.c_str());
}

struct Register {
    Register(const char* name, void (*fn)()) { registry().cases.push_back({name, fn}); }
};

// Converts a value to text for failure messages.
template <typename T>
std::string to_text(const T& v) {
    if constexpr (std::is_same_v<T, std::string>) {
        return v;
    } else if constexpr (std::is_same_v<T, bool>) {
        return v ? "true" : "false";
    } else if constexpr (std::is_arithmetic_v<T>) {
        return std::to_string(v);
    } else {
        std::ostringstream os;
        os << v;
        return os.str();
    }
}

inline int run_all() {
    int failed_cases = 0;
    for (auto& c : registry().cases) {
        registry().current = c.name;
        const int before = registry().failures;
        c.fn();
        const bool ok = registry().failures == before;
        std::printf("%-34s %s\n", c.name, ok ? "ok" : "FAILED");
        if (!ok) ++failed_cases;
    }
    std::printf("%d cases, %d failures\n", static_cast<int>(registry().cases.size()),
                registry().failures);
    return failed_cases == 0 ? 0 : 1;
}

} // namespace wj::test

#define WJ_TEST(name)                                                                  \
    static void name();                                                                \
    static ::wj::test::Register reg_##name(#name, name);                             \
    static void name()

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) ::wj::test::fail(__FILE__, __LINE__, "CHECK(" #cond ")");         \
    } while (0)

#define CHECK_EQ(a, b)                                                                 \
    do {                                                                               \
        const auto va_ = (a);                                                          \
        const auto vb_ = (b);                                                          \
        if (!(va_ == vb_))                                                             \
            ::wj::test::fail(__FILE__, __LINE__,                                        \
                             std::string(#a " == " #b " (") + ::wj::test::to_text(va_) + \
                                 " vs " + ::wj::test::to_text(vb_) + ")");             \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                          \
    do {                                                                               \
        const double va_ = static_cast<double>(a);                                     \
        const double vb_ = static_cast<double>(b);                                     \
        if (std::fabs(va_ - vb_) > (eps))                                              \
            ::wj::test::fail(__FILE__, __LINE__,                                        \
                             std::string(#a " ~= " #b " (") + ::wj::test::to_text(va_) + \
                                 " vs " + ::wj::test::to_text(vb_) + ")");             \
    } while (0)
