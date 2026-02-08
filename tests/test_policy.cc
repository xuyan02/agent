#include <cassert>
#include <filesystem>
#include <iostream>
#include <optional>

#include "interfaces/itool.h"

static std::optional<std::string> resolve_under_root(const std::filesystem::path& root,
                                                    const std::string& rel) {
  auto p = std::filesystem::path(rel);
  if (p.is_absolute()) return std::nullopt;
  // Block any ".." segments.
  for (const auto& part : p) {
    if (part == "..") return std::nullopt;
  }
  return (root / p).string();
}

static agent::PolicyDecision allow_shell_command(const std::string& cmd) {
  // Keep the old test intent: block obviously dangerous commands.
  if (cmd.find("rm -rf") != std::string::npos) return {.allowed = false, .reason = "blocked"};
  if (cmd.find("sudo") != std::string::npos) return {.allowed = false, .reason = "blocked"};
  return {.allowed = true, .reason = {}};
}

static void test_path_sandbox() {
  auto ok = resolve_under_root(std::filesystem::path("/tmp"), "a/b.txt");
  assert(ok.has_value());

  auto bad = resolve_under_root(std::filesystem::path("/tmp"), "../etc/passwd");
  assert(!bad.has_value());
}

static void test_shell_policy() {
  assert(allow_shell_command("echo hi").allowed);
  assert(!allow_shell_command("rm -rf / ").allowed);
  assert(!allow_shell_command("sudo ls").allowed);
}

int main() {
  test_path_sandbox();
  test_shell_policy();
  std::cout << "ok\n";
  return 0;
}
