#include "infra/tools/file_tools.h"

#include <fstream>
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

agent::ToolResult ReadFileTool::Invoke(const std::string& tool_call_id,
                                                const std::string& arguments_json,
                                                const agent::ToolContext& ctx) {
  agent::ToolResult tr;
  tr.tool_call_id = tool_call_id;

  auto path_str = extract_json_string_or_empty(arguments_json, "path");
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

  auto path_str = extract_json_string_or_empty(arguments_json, "path");
  auto content = extract_json_string_or_empty(arguments_json, "content");
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
