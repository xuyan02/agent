
#include "agent/smart/tools/shallow_think_tool.h"

#include <utility>

namespace agent {

namespace {

constexpr const char* kParametersJson = R"JSON({
  "type": "object",
  "properties": {
    "thought": {"type": "string", "description": "IntuitiveAgent's short thought (no chain-of-thought)."},
    "content": {"type": "string", "description": "User request / content."}
  },
  "required": ["thought", "content"],
  "additionalProperties": false
})JSON";

}  // namespace

ShallowThinkTool::ShallowThinkTool(agent::SmartAgent* smart) : smart_(smart) {
  id = "shallow_think_tool";
  description = "Shallow thinking toolset";
}

void ShallowThinkTool::Init() {
  RegisterFunction(
      "shallow_think_tool.think",
      "Do a shallow thinking pass and return the result.",
      kParametersJson,
      [this](nlohmann::json arguments, agent::Function::OnDone on_done, agent::Function::OnError on_error) {
        ShallowThink(std::move(arguments), std::move(on_done), std::move(on_error));
      });
}

void ShallowThinkTool::ShallowThink(nlohmann::json arguments,
                                    agent::Function::OnDone on_done,
                                    agent::Function::OnError on_error) {
  if (!smart_) {
    if (on_error)
      std::move(on_error)("null_smart_agent");
    return;
  }

  if (!arguments.is_object()) {
    if (on_error)
      std::move(on_error)("invalid_arguments");
    return;
  }

  const std::string thought = arguments.value("thought", "");
  const std::string content = arguments.value("content", "");
  if (thought.empty() || content.empty()) {
    if (on_error)
      std::move(on_error)("missing_required_fields");
    return;
  }

  smart_->RunShallowThink(
      thought,
      content,
      [on_done = std::move(on_done)](std::string out) mutable {
        if (!on_done)
          return;
        nlohmann::json r;
        r["output"] = std::move(out);
        std::move(on_done)(std::move(r));
      },
      [on_error = std::move(on_error)](std::string error) mutable {
        if (on_error)
          std::move(on_error)(std::move(error));
      });
}

}  // namespace agent
