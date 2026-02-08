#include "agent/session.h"

#include "agent/in_memory_history.h"

#include "dust/async/future.h"
#include "dust/memory/ref_ptr.h"

#include <filesystem>
#include <string>
#include <utility>

namespace agent {

const dust::RefPtr<History>& Session::history() const {
  return history_;
}

const std::vector<dust::RefPtr<SystemPromptSegment>>& Session::system_segments() const {
  return system_segments_;
}

const std::vector<dust::RefPtr<Tool>>& Session::tools() const {
  return tools_;
}

const std::vector<std::unique_ptr<LlmProvider>>& Session::llm_providers() const {
  return llm_providers_;
}

const std::string& Session::default_model() const {
  return default_model_;
}

const std::filesystem::path& Session::workspace_path() const {
  return workspace_path_;
}

const std::filesystem::path& Session::agent_path() const {
  return agent_path_;
}

std::unique_ptr<LlmSender> Session::CreateSender(std::string model) const {
  for (const auto& provider : llm_providers_) {
    if (!provider)
      continue;
    std::unique_ptr<LlmSender> sender = provider->CreateSender(model);
    if (sender)
      return sender;
  }
  return nullptr;
}

ToolCallExecutor* Session::tool_call_executor() const {
  if (!tool_call_executor_)
    tool_call_executor_ = dust::MakeRefPtr<ToolCallExecutor>();
  return tool_call_executor_.Get();
}

Session::Builder& Session::Builder::SetHistory(dust::RefPtr<History> history) {
  history_ = std::move(history);
  return *this;
}

Session::Builder& Session::Builder::AddSystemSegment(dust::RefPtr<SystemPromptSegment> segment) {
  if (segment)
    system_segments_.push_back(std::move(segment));
  return *this;
}

Session::Builder& Session::Builder::AddTool(dust::RefPtr<Tool> tool) {
  if (tool)
    tools_.push_back(std::move(tool));
  return *this;
}

Session::Builder& Session::Builder::AddLlmProvider(std::unique_ptr<LlmProvider> provider) {
  if (provider)
    llm_providers_.push_back(std::move(provider));
  return *this;
}

Session::Builder& Session::Builder::SetDefaultModel(std::string model) {
  default_model_ = std::move(model);
  return *this;
}

Session::Builder& Session::Builder::SetWorkspacePath(std::filesystem::path workspace_path) {
  workspace_path_ = std::move(workspace_path);
  return *this;
}

Session::Builder& Session::Builder::SetAgentPath(std::filesystem::path agent_path) {
  agent_path_ = std::move(agent_path);
  return *this;
}

// Session::Builder

dust::RefPtr<Session> Session::Builder::Build() && {
  dust::RefPtr<History> history = std::move(history_);
  if (!history)
    history = dust::MakeRefPtr<InMemoryHistory>();


  std::filesystem::path workspace_path = std::move(workspace_path_);
  if (workspace_path.empty())
    workspace_path = std::filesystem::current_path();

  std::filesystem::path agent_path = std::move(agent_path_);
  if (agent_path.empty())
    agent_path = workspace_path / ".agent";

  return dust::RefPtr<Session>::Adopt(new Session(std::move(history),
                                                 std::move(system_segments_),
                                                 std::move(tools_),
                                                 std::move(llm_providers_),
                                                 std::move(default_model_),
                                                 std::move(workspace_path),
                                                 std::move(agent_path)));
}

}  // namespace agent
