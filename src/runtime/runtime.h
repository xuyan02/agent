#pragma once

#include "infra/llm/llm_context.h"
#include "infra/llm/llm_request.h"
#include "tool/tool.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace agent {

// Framework runtime services shared by agents.
// Minimal v1: a thin wrapper over LlmContext::Create().
class Runtime {
 public:
  explicit Runtime(agent::LlmContext* llm);

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  std::unique_ptr<agent::LlmRequest> CreateRequest(std::string model_name,
                                                   std::vector<agent::LlmMessage> messages,
                                                   std::vector<agent::Tool*> tools,
                                                   agent::LlmRequest::OnToken on_token,
                                                   agent::LlmRequest::OnToolCalls on_tool_calls,
                                                   agent::LlmRequest::OnDone on_done);

  void RegisterTool(agent::ToolPtr t);

  // Find a tool by its id/name (e.g. "file").
  // Returns nullptr if not found.
  agent::Tool* FindTool(const std::string& tool_id) const;

 private:
  agent::LlmContext* llm_{nullptr};
  std::unordered_map<std::string, agent::ToolPtr> tools_;
};

}  // namespace agent
