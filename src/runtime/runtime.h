#pragma once

#include "agent/agent_factory.h"
#include "interfaces/iconsole.h"

#include "llm/llm_context.h"
#include "llm/llm_request.h"
#include "tool/tool.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace agent {

class Agent;

class Runtime {
 public:
  // Console must outlive Runtime.
  // If root_path is empty, the default path is computed from the current user's
  // home directory (pw_dir) + ".agent".
  Runtime(agent::IConsole& console, std::string root_path);

  const std::filesystem::path& GetRootPath() const { return root_path_; }

  std::string GetPrompt(const std::string& name) const;

  agent::IConsole& console() { return console_; }
  const agent::IConsole& console() const { return console_; }

  agent::LlmContext* llm() { return llm_.get(); }
  const agent::LlmContext* llm() const { return llm_.get(); }

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  std::unique_ptr<agent::LlmRequest> CreateRequest(std::string model_name,
                                                   std::vector<agent::LlmMessage> messages,
                                                   std::vector<agent::Tool*> tools,
                                                   agent::LlmRequest::OnToken on_token,
                                                   agent::LlmRequest::OnToolCalls on_tool_calls,
                                                   agent::LlmRequest::OnDone on_done);

  // Initializes runtime services from <root_path>/runtime.json.
  // Returns false on any load/parse/registration error.
  bool Init();

  void RegisterAgentFactory(std::unique_ptr<agent::AgentFactory> factory);

  agent::Agent* GetMainAgent() { return main_agent_.get(); }

  void RegisterTool(agent::ToolPtr t);

  // Find a tool by its id/name (e.g. "file").
  // Returns nullptr if not found.
  agent::Tool* FindTool(const std::string& tool_id) const;

 private:
  static std::filesystem::path ComputeDefaultRootPath();

  agent::IConsole& console_;

  const agent::AgentFactory* FindAgentFactory(const std::string& type) const;

  std::unique_ptr<agent::LlmContext> llm_;
  std::filesystem::path root_path_;
  std::vector<std::unique_ptr<agent::AgentFactory>> agent_factories_;
  std::unique_ptr<agent::Agent> main_agent_;
  std::unordered_map<std::string, agent::ToolPtr> tools_;
};

}  // namespace agent
