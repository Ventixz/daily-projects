// A minimal test framework: TEST registers a function, CHECK throws on failure.
#pragma once

#include <exception>
#include <sstream>
#include <string>
#include <vector>

namespace testing {

struct Case {
    const char *name;
    void (*fn)();
};

inline std::vector<Case> &registry() {
    static std::vector<Case> cases;
    return cases;
}

struct Registrar {
    Registrar(const char *name, void (*fn)()) { registry().push_back(Case{name, fn}); }
};

struct Failure {
    std::string message;
};

}  // namespace testing

#define TEST(name)                                                       \
    static void test_##name();                                           \
    static testing::Registrar registrar_##name(#name, &test_##name);     \
    static void test_##name()

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            throw testing::Failure{std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
                                   ": CHECK(" #cond ") failed"};                         \
        }                                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        const auto &check_a_ = (a);                                                     \
        const auto &check_b_ = (b);                                                     \
        if (!(check_a_ == check_b_)) {                                                  \
            std::ostringstream check_os_;                                               \
            check_os_ << __FILE__ << ":" << __LINE__ << ": CHECK_EQ(" #a ", " #b ")\n"  \
                      << "    left:  " << check_a_ << "\n    right: " << check_b_;     \
            throw testing::Failure{check_os_.str()};                                    \
        }                                                                               \
    } while (0)
