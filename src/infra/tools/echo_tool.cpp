#include "infra/tools/echo_tool.h"

#include "infra/json/json.h"

#include <nlohmann/json.hpp>

#include <string>

namespace agent {

agent::ToolResult EchoTool::Invoke(const std::string& tool_call_id,
                                const std::string& arguments_json,
                                const agent::ToolContext& /*ctx*/) {
  agent::ToolResult tr;
  tr.tool_call_id = tool_call_id;

  auto args_opt = agent::json::Parse(arguments_json);
  if (!args_opt || !args_opt->is_object()) {
    tr.ok = false;
    tr.content = "Invalid JSON arguments";
    return tr;
  }

  auto it = args_opt->find("message");
  if (it == args_opt->end() || !it->is_string()) {
    tr.ok = false;
    tr.content = "Missing argument: message";
    return tr;
  }

  auto msg = it->get<std::string>();
  if (msg.empty()) {
    tr.ok = false;
    tr.content = "Missing argument: message";
    return tr;
  }

  tr.ok = true;
  tr.content = msg;
  return tr;
}

} // namespace agent
