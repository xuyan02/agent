#include "infra/tools/file_tools.h"

#include "infra/json/json.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace agent {

agent::ToolResult ReadFileTool::Invoke(const std::string& tool_call_id,
                                       const std::string& arguments_json,
                                       const agent::ToolContext& ctx) {
  agent::ToolResult tr;
  tr.tool_call_id = tool_call_id;

  auto args_opt = agent::json::Parse(arguments_json);
  if (!args_opt) {
    tr.ok = false;
    tr.content = "Invalid JSON arguments";
    return tr;
  }

  const auto& args = *args_opt;
  if (!args.is_object()) {
    tr.ok = false;
    tr.content = "Invalid JSON arguments";
    return tr;
  }

  auto it = args.find("path");
  if (it == args.end() || !it->is_string()) {
    tr.ok = false;
    tr.content = "Missing argument: path";
    return tr;
  }
  auto path_str = it->get<std::string>();

  if (!ctx.resolve_under_root) {
    tr.ok = false;
    tr.content = "No policy: resolve_under_root";
    return tr;
  }

  auto resolved = ctx.resolve_under_root(path_str);
  if (!resolved) {
    tr.ok = false;
    tr.content = "Path denied by policy";
    return tr;
  }

  std::ifstream ifs(*resolved);
  if (!ifs) {
    tr.ok = false;
    tr.content = "Failed to open file";
    return tr;
  }

  std::ostringstream oss;
  oss << ifs.rdbuf();
  tr.content = oss.str();
  return tr;
}

agent::ToolResult WriteFileTool::Invoke(const std::string& tool_call_id,
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

  const auto& args = *args_opt;

  auto pit = args.find("path");
  if (pit == args.end() || !pit->is_string()) {
    tr.ok = false;
    tr.content = "Missing argument: path";
    return tr;
  }
  auto path_str = pit->get<std::string>();

  auto cit = args.find("content");
  std::string content;
  if (cit != args.end() && cit->is_string())
    content = cit->get<std::string>();

  if (path_str.empty()) {
    tr.ok = false;
    tr.content = "Missing argument: path";
    return tr;
  }

  if (!ctx.resolve_under_root) {
    tr.ok = false;
    tr.content = "No policy: resolve_under_root";
    return tr;
  }

  auto resolved = ctx.resolve_under_root(path_str);
  if (!resolved) {
    tr.ok = false;
    tr.content = "Path denied by policy";
    return tr;
  }

  std::ofstream ofs(*resolved);
  if (!ofs) {
    tr.ok = false;
    tr.content = "Failed to write file";
    return tr;
  }
  ofs << content;
  tr.content = "ok";
  return tr;
}

} // namespace agent
