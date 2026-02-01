#include "runtime/runtime.h"

#include <utility>

namespace agent {

Runtime::Runtime(agent::LlmContext* llm) : llm_(llm) {}

std::unique_ptr<agent::LlmRequest> Runtime::CreateRequest(
    std::string model_name,
    std::vector<agent::LlmMessage> messages,
    std::vector<agent::Tool> tools,
    agent::LlmRequest::OnToken on_token,
    agent::LlmRequest::OnToolCalls on_tool_calls,
    agent::LlmRequest::OnDone on_done) {
  if (!llm_)
    return nullptr;
  return llm_->Create(std::move(model_name), std::move(messages), std::move(tools),
                      std::move(on_token), std::move(on_tool_calls), std::move(on_done));
}

void Runtime::RegisterTool(agent::ToolPtr t) {
  if (!t)
    return;

  tools_[t->id] = std::move(t);
}

agent::Function* Runtime::FindFunction(const std::string& function_name) const {
  const auto dot = function_name.find('.');
  if (dot == std::string::npos)
    return nullptr;

  const std::string tool_name = function_name.substr(0, dot);
  const std::string fn_name = function_name.substr(dot + 1);

  auto it = tools_.find(tool_name);
  if (it == tools_.end() || !it->second)
    return nullptr;

  return it->second->FindFunctionByName(function_name);
}

}  // namespace agent
