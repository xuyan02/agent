#include "infra/tools/echo_tool.h"

#include <string>

namespace {

static std::string extract_json_string_or_empty(const std::string& json, const std::string& key) {
  auto pos = json.find('"' + key + '"');
  if (pos == std::string::npos) return "";
  pos = json.find(':', pos);
  if (pos == std::string::npos) return "";
  pos++;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) pos++;
  if (pos >= json.size() || json[pos] != '"') return "";
  pos++;

  std::string out;
  for (; pos < json.size(); ++pos) {
    char c = json[pos];
    if (c == '\\') {
      if (pos + 1 >= json.size()) break;
      char n = json[pos + 1];
      if (n == 'n') out.push_back('\n');
      else if (n == 'r') out.push_back('\r');
      else if (n == 't') out.push_back('\t');
      else out.push_back(n);
      pos++;
      continue;
    }
    if (c == '"') break;
    out.push_back(c);
  }
  return out;
}

} // namespace

namespace cpp_agent::infra::tools {

cpp_agent::core::ToolResult EchoTool::invoke(const std::string& tool_call_id,
                                            const std::string& arguments_json,
                                            const cpp_agent::interfaces::ToolContext& /*ctx*/) {
  cpp_agent::core::ToolResult tr;
  tr.tool_call_id = tool_call_id;

  auto msg = extract_json_string_or_empty(arguments_json, "message");
  if (msg.empty()) {
    tr.ok = false;
    tr.content = "Missing argument: message";
    return tr;
  }

  tr.ok = true;
  tr.content = msg;
  return tr;
}

} // namespace cpp_agent::infra::tools
