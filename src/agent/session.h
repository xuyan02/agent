#pragma once

#include "agent/history.h"
#include "agent/system_prompt_segment.h"
#include "agent/tool_call_executor.h"
#include "llm/llm_provider.h"
#include "llm/llm_sender.h"
#include "tool/tool.h"

#include "dust/memory/ref_counted.h"
#include "dust/memory/ref_ptr.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace agent {

class Session final : public dust::RefCounted {
 public:
  class Builder;

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  const dust::RefPtr<History>& history() const;
  const std::vector<dust::RefPtr<SystemPromptSegment>>& system_segments() const;
  const std::vector<dust::RefPtr<Tool>>& tools() const;
  const std::vector<std::unique_ptr<LlmProvider>>& llm_providers() const;
  const std::string& default_model() const;

  const std::filesystem::path& workspace_path() const;
  const std::filesystem::path& agent_path() const;

  std::unique_ptr<LlmSender> CreateSender(std::string model) const;

  // Lazily created and owned by Session. Not exposed via Builder.
  ToolCallExecutor* tool_call_executor() const;


 private:
  friend class Builder;

  Session() = delete;

  Session(dust::RefPtr<History> history,
          std::vector<dust::RefPtr<SystemPromptSegment>> system_segments,
          std::vector<dust::RefPtr<Tool>> tools,
          std::vector<std::unique_ptr<LlmProvider>> llm_providers,
          std::string default_model,
          std::filesystem::path workspace_path,
          std::filesystem::path agent_path)
      : history_(std::move(history)),
        system_segments_(std::move(system_segments)),
        tools_(std::move(tools)),
        llm_providers_(std::move(llm_providers)),
        default_model_(std::move(default_model)),
        workspace_path_(std::move(workspace_path)),
        agent_path_(std::move(agent_path)) {}

  dust::RefPtr<History> history_;
  std::vector<dust::RefPtr<SystemPromptSegment>> system_segments_;
  std::vector<dust::RefPtr<Tool>> tools_;
  std::vector<std::unique_ptr<LlmProvider>> llm_providers_;
  std::string default_model_;
  std::filesystem::path workspace_path_;
  std::filesystem::path agent_path_;

  // Internal helper for tool-call loop.
  mutable dust::RefPtr<ToolCallExecutor> tool_call_executor_;
};

class Session::Builder {
 public:
  Builder() = default;

  Builder(const Builder&) = delete;
  Builder& operator=(const Builder&) = delete;

  Builder& SetHistory(dust::RefPtr<History> history);

  Builder& AddSystemSegment(dust::RefPtr<SystemPromptSegment> segment);

  Builder& AddTool(dust::RefPtr<Tool> tool);

  Builder& AddLlmProvider(std::unique_ptr<LlmProvider> provider);

  Builder& SetDefaultModel(std::string model);

  Builder& SetWorkspacePath(std::filesystem::path workspace_path);
  Builder& SetAgentPath(std::filesystem::path agent_path);

  dust::RefPtr<Session> Build() &&;

 private:
  dust::RefPtr<History> history_;
  std::vector<dust::RefPtr<SystemPromptSegment>> system_segments_;
  std::vector<dust::RefPtr<Tool>> tools_;
  std::vector<std::unique_ptr<LlmProvider>> llm_providers_;
  std::string default_model_;
  std::filesystem::path workspace_path_;
  std::filesystem::path agent_path_;
};

}  // namespace agent
