#include "core/policy.h"

#include <cassert>
#include <filesystem>
#include <iostream>

static void test_path_sandbox() {
  cpp_agent::core::Policy p(std::filesystem::path("/tmp"));

  auto ok = p.resolve_under_root("a/b.txt");
  assert(ok.has_value());

  auto bad = p.resolve_under_root("../etc/passwd");
  assert(!bad.has_value());
}

static void test_shell_policy() {
  cpp_agent::core::Policy p(std::filesystem::path("/tmp"));

  assert(p.allow_shell_command("echo hi").allowed);
  assert(!p.allow_shell_command("rm -rf / ").allowed);
  assert(!p.allow_shell_command("sudo ls").allowed);
}

int main() {
  test_path_sandbox();
  test_shell_policy();
  std::cout << "ok\n";
  return 0;
}
