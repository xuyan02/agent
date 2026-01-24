#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace cpp_agent::core {

struct PolicyDecision {
  bool allowed{false};
  std::string reason;
};

class Policy final {
public:
  explicit Policy(std::filesystem::path project_root);

  [[nodiscard]] const std::filesystem::path& project_root() const { return project_root_; }

  // Path sandbox: only allow paths under project_root.
  [[nodiscard]] std::optional<std::filesystem::path> resolve_under_root(
      const std::filesystem::path& user_path) const;

  [[nodiscard]] PolicyDecision allow_shell_command(const std::string& command) const;

private:
  std::filesystem::path project_root_;
};

} // namespace cpp_agent::core
