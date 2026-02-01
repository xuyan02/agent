#include "infra/tools/shell_tool.h"

#include "infra/json/json.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <future>
#include <sstream>

namespace agent {

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
  agent::ToolResult tr;
  tr.tool_call_id = tool_call_id;

  auto args_opt = agent::json::Parse(arguments_json);
  if (!args_opt || !args_opt->is_object()) {
    tr.ok = false;
    tr.content = "Invalid JSON arguments";
    return tr;
  }

  auto it = args_opt->find("command");
  if (it == args_opt->end() || !it->is_string()) {
    tr.ok = false;
    tr.content = "Missing argument: command";
    return tr;
  }
  auto cmd = it->get<std::string>();

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
