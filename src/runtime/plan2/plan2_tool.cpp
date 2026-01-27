#include "runtime/plan2/plan2_tool.h"

#include <sstream>

namespace agent::plan2 {
namespace {

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
    const char c = json[pos];
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

} // namespace

PlanTool::PlanTool(PlanModel* plan) : plan_(plan) {}

std::string PlanTool::Name() const { return "plan"; }

agent::ToolResult PlanTool::Invoke(const std::string& tool_call_id,
                                   const std::string& arguments_json,
                                   const agent::ToolContext& /*ctx*/) {
  agent::ToolResult tr;
  tr.tool_call_id = tool_call_id;

  // This PlanTool instance represents a Tool (collection of functions).
  // The built-in tool calling routes by name at the tool-call level; function
  // routing is handled by the ToolManager (not implemented yet).
  // For now, keep a placeholder implementation.
  tr.ok = false;
  tr.content = "plan tool not wired: function routing not implemented";
  (void)arguments_json;
  return tr;
}

} // namespace agent::plan2
