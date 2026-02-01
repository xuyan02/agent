#include "runtime/plan2/plan2_tool.h"

#include "infra/json/json.h"

#include <nlohmann/json.hpp>

#include <sstream>

namespace agent::plan2 {

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
