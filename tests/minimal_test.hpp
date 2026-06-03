#pragma once

#include <cstdlib>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace minimal_test {

struct Failure {
    std::string file;
    int line = 0;
    std::string message;
};

inline std::vector<Failure>& failures() {
    static std::vector<Failure> f;
    return f;
}

inline int& failCount() {
    static int n = 0;
    return n;
}

struct Registrar {
    Registrar(const char* suite, const char* name, std::function<void()> fn) {
        runners().push_back({suite, name, std::move(fn)});
    }

    struct Entry {
        const char* suite;
        const char* name;
        std::function<void()> fn;
    };

    static std::vector<Entry>& runners() {
        static std::vector<Entry> r;
        return r;
    }
};

inline void report(const char* file, int line, const std::string& msg) {
    failures().push_back({file, line, msg});
    ++failCount();
    std::cerr << file << ":" << line << ": Failure\n" << msg << "\n";
}

}  // namespace minimal_test

#define MT_TEST(suite, name)                                                       \
    static void mt_##suite##_##name();                                             \
    static minimal_test::Registrar mt_reg_##suite##_##name(                        \
        #suite, #name, &mt_##suite##_##name);                                      \
    static void mt_##suite##_##name()

#define EXPECT_TRUE(expr)                                                          \
    do {                                                                           \
        if (!(expr)) {                                                             \
            minimal_test::report(__FILE__, __LINE__,                               \
                                 std::string("Expected true: ") + #expr);          \
        }                                                                          \
    } while (0)

#define ASSERT_TRUE(expr)                                                          \
    do {                                                                           \
        if (!(expr)) {                                                             \
            minimal_test::report(__FILE__, __LINE__,                               \
                                 std::string("Assert true: ") + #expr);            \
            return;                                                                \
        }                                                                          \
    } while (0)

#define EXPECT_EQ(a, b)                                                            \
    do {                                                                           \
        if (!((a) == (b))) {                                                       \
            minimal_test::report(__FILE__, __LINE__,                               \
                                 std::string("Expected equality: ") + #a +       \
                                     " == " + #b);                                 \
        }                                                                          \
    } while (0)

#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))

#define EXPECT_NE(a, b)                                                            \
    do {                                                                           \
        if ((a) == (b)) {                                                          \
            minimal_test::report(__FILE__, __LINE__, "Expected not equal");        \
        }                                                                          \
    } while (0)

#define EXPECT_GE(a, b) EXPECT_TRUE((a) >= (b))

#define ASSERT_EQ(a, b)                                                            \
    do {                                                                           \
        if (!((a) == (b))) {                                                       \
            minimal_test::report(__FILE__, __LINE__,                               \
                                 std::string("Assert equality: ") + #a + " == " +  \
                                     #b);                                          \
            return;                                                                \
        }                                                                          \
    } while (0)

#define ASSERT_GE(a, b)                                                            \
    do {                                                                           \
        if (!((a) >= (b))) {                                                       \
            minimal_test::report(__FILE__, __LINE__,                               \
                                 std::string("Assert >=: ") + #a + " >= " + #b);   \
            return;                                                                \
        }                                                                          \
    } while (0)

inline int RUN_ALL_TESTS() {
    using minimal_test::Registrar;
    int run = 0;
    for (const auto& e : Registrar::runners()) {
        ++run;
        std::cout << "[ RUN      ] " << e.suite << "." << e.name << "\n";
        e.fn();
        std::cout << "[       OK ] " << e.suite << "." << e.name << "\n";
    }
    if (minimal_test::failCount() > 0) {
        std::cerr << minimal_test::failCount() << " failures, " << run << " tests\n";
        return 1;
    }
    std::cout << "PASSED " << run << " tests\n";
    return 0;
}
