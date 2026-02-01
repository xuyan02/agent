#include "infra/tools/shell_tool.h"

#include <chrono>
#include <cstdio>
#include <future>
#include <sstream>

namespace agent {

static std::string extract_json_string_or_empty(const std::string& json, const std::string& key) {
  auto pos = json.find('"' + key + '"');
  if (pos == std::string::npos) return {};
  pos = json.find(':', pos);
  if (pos == std::string::npos) return {};
  pos++;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) pos++;
  if (pos >= json.size() || json[pos] != '"') return {};
  pos++;
  std::string out;
  for (; pos < json.size(); ++pos) {
    char c = json[pos];
    if (c == '\\') {
      if (pos + 1 >= json.size()) break;
      out.push_back(json[pos + 1]);
      pos++;
      continue;
    }
    if (c == '"') break;
    out.push_back(c);
  }
  return out;
}

ShellTool::ShellTool(int timeout_ms) : timeout_ms_(timeout_ms) {}

static agent::ToolResult run_with_popen(const std::string& tool_call_id,
                                                 const std::string& command) {
  agent::ToolResult tr;
  tr.tool_call_id = tool_call_id;

  std::string full = "bash -lc \"" + command + "\" 2>&1";
  FILE* pipe = popen(full.c_str(), "r");
  if (!pipe) {
    tr.ok = false;
    tr.content = "Failed to start process";
    return tr;
  }

  std::ostringstream oss;
  char buf[4096];
  while (fgets(buf, sizeof(buf), pipe)) {
    oss << buf;
  }
  int rc = pclose(pipe);
  (void)rc;
  tr.content = oss.str();
  return tr;
}

agent::ToolResult ShellTool::Invoke(const std::string& tool_call_id,
                                             const std::string& arguments_json,
                                             const agent::ToolContext& ctx) {
  auto cmd = extract_json_string_or_empty(arguments_json, "command");

  agent::ToolResult tr;
  tr.tool_call_id = tool_call_id;

  if (cmd.empty()) {
    tr.ok = false;
    tr.content = "Missing argument: command";
    return tr;
  }

  if (!ctx.allow_shell_command) {
    tr.ok = false;
    tr.content = "No policy: allow_shell_command";
    return tr;
  }

  auto decision = ctx.allow_shell_command(cmd);
  if (!decision.allowed) {
    tr.ok = false;
    tr.content = decision.reason;
    return tr;
  }

  // timeout via async+wait; does not kill process in MVP.
  auto fut = std::async(std::launch::async, [tool_call_id, cmd] { return run_with_popen(tool_call_id, cmd); });
  if (fut.wait_for(std::chrono::milliseconds(timeout_ms_)) == std::future_status::timeout) {
    tr.ok = false;
    tr.content = "Command timed out";
    return tr;
  }
  return fut.get();
}

} // namespace agent
