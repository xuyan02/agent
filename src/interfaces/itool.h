#pragma once

#include <optional>
#include <string>

namespace agent {

struct ToolResult {
  std::string tool_call_id;
  bool ok{true};
  std::string content;
};

struct PolicyDecision {
  bool allowed{false};
  std::string reason;
};

struct ToolContext {
  // A minimal, temporary policy interface while core/ is removed.
  // Callers can supply lambdas to control access.
  std::optional<std::string> (*resolve_under_root)(const std::string& path){nullptr};
  PolicyDecision (*allow_shell_command)(const std::string& cmd){nullptr};
};

class ITool {
public:
  virtual ~ITool() = default;
  [[nodiscard]] virtual std::string Name() const = 0;
  virtual ToolResult Invoke(const std::string& tool_call_id,
                            const std::string& arguments_json,
                            const ToolContext& ctx) = 0;
};

} // namespace agent
