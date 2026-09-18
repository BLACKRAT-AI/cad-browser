#include "cad_test.h"

namespace cad_test {
namespace {

int g_failures_in_current_test = 0;
int g_total_failures = 0;

}  // namespace

std::vector<TestCase>& Registry() {
  static std::vector<TestCase> registry;
  return registry;
}

void ReportFailure(const char* file, int line, const std::string& message) {
  ++g_failures_in_current_test;
  ++g_total_failures;
  std::printf("    FAIL %s:%d\n      %s\n", file, line, message.c_str());
}

int RunAll() {
  int passed = 0;
  int failed = 0;

  std::printf("Running %d tests\n\n", static_cast<int>(Registry().size()));
  for (const TestCase& test : Registry()) {
    g_failures_in_current_test = 0;
    std::printf("  %s\n", test.name);
    test.fn();
    if (g_failures_in_current_test == 0) {
      ++passed;
    } else {
      ++failed;
    }
  }

  std::printf("\n%d passed, %d failed, %d assertions failed\n", passed, failed,
              g_total_failures);
  return failed == 0 ? 0 : 1;
}

}  // namespace cad_test

int main() {
  return cad_test::RunAll();
}
