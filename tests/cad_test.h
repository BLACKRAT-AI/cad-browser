// CAD Browser - minimal test harness.
//
// Dependency-free on purpose: CEF builds with exceptions and RTTI disabled,
// which rules out most frameworks' assertion machinery, and the handful of
// things worth testing here do not need more than this.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace cad_test {

struct TestCase {
  const char* name;
  void (*fn)();
};

std::vector<TestCase>& Registry();
int RunAll();

void ReportFailure(const char* file, int line, const std::string& message);

// ---- value printing for failure messages ----
inline std::string Describe(bool v) { return v ? "true" : "false"; }
inline std::string Describe(int v) { return std::to_string(v); }
inline std::string Describe(long v) { return std::to_string(v); }
inline std::string Describe(long long v) { return std::to_string(v); }
inline std::string Describe(unsigned v) { return std::to_string(v); }
inline std::string Describe(unsigned long v) { return std::to_string(v); }
inline std::string Describe(unsigned long long v) { return std::to_string(v); }
inline std::string Describe(double v) { return std::to_string(v); }
inline std::string Describe(const char* v) {
  return v ? std::string("\"") + v + "\"" : std::string("(null)");
}
inline std::string Describe(const std::string& v) { return "\"" + v + "\""; }

struct Registrar {
  Registrar(const char* name, void (*fn)()) { Registry().push_back({name, fn}); }
};

}  // namespace cad_test

#define CAD_TEST(test_name)                                          \
  static void test_name();                                           \
  static ::cad_test::Registrar test_name##_registrar(#test_name,     \
                                                     &test_name);    \
  static void test_name()

#define CAD_EXPECT(condition)                                        \
  do {                                                               \
    if (!(condition)) {                                              \
      ::cad_test::ReportFailure(__FILE__, __LINE__,                  \
                                "expected: " #condition);            \
    }                                                                \
  } while (false)

#define CAD_EXPECT_EQ(actual, expected)                              \
  do {                                                               \
    const auto& cad_actual = (actual);                               \
    const auto& cad_expected = (expected);                           \
    if (!(cad_actual == cad_expected)) {                             \
      ::cad_test::ReportFailure(                                     \
          __FILE__, __LINE__,                                        \
          std::string(#actual) + " == " + #expected + "\n      got: " + \
              ::cad_test::Describe(cad_actual) + "\n      want: " +  \
              ::cad_test::Describe(cad_expected));                   \
    }                                                                \
  } while (false)

#define CAD_EXPECT_NE(actual, unexpected)                            \
  do {                                                               \
    if ((actual) == (unexpected)) {                                  \
      ::cad_test::ReportFailure(__FILE__, __LINE__,                  \
                                #actual " should not equal " #unexpected); \
    }                                                                \
  } while (false)
