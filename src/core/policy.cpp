#include "core/policy.h"

#include <algorithm>

namespace agent {

static bool starts_with(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), s.begin());
}

Policy::Policy(std::filesystem::path project_root) {
  std::error_code ec;
  auto canon = std::filesystem::weakly_canonical(project_root, ec);
  project_root_ = ec ? std::move(project_root) : std::move(canon);
}

std::optional<std::filesystem::path> Policy::resolve_under_root(
    const std::filesystem::path& user_path) const {
  std::filesystem::path candidate = user_path.is_absolute() ? user_path : (project_root_ / user_path);

  std::error_code ec;
  auto canon = std::filesystem::weakly_canonical(candidate, ec);
  if (ec) return std::nullopt;

  auto root = project_root_;
  auto root_str = root.string();
  if (!root_str.empty() && root_str.back() != '/') root_str.push_back('/');

  auto canon_str = canon.string();
  if (canon_str == root.string() || starts_with(canon_str, root_str)) {
    return canon;
  }
  return std::nullopt;
}

PolicyDecision Policy::allow_shell_command(const std::string& command) const {
  // MVP deny-list; can evolve into allow-list + confirmation strategy.
  static const char* kDeniedTokens[] = {
      "rm ", "rm-", " rm", "sudo ", "mkfs", "dd ", "shutdown", "reboot", "kill ",
      ">/", ":(){:|:&};:",
  };

  for (const auto* tok : kDeniedTokens) {
    if (command.find(tok) != std::string::npos) {
      return PolicyDecision{false, std::string("Denied by policy (token): ") + tok};
    }
  }
  return PolicyDecision{true, ""};
}

} // namespace agent
