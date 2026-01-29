#pragma once

#include "runtime/agent.h"
#include "runtime/message.h"
#include "runtime/plan2/plan2_model.h"

#include <deque>
#include <string>

namespace agent {

class Team;

class GeneralAgent final : public Agent {
public:
  GeneralAgent(Team& team,
               std::string name,
               std::string model);
  ~GeneralAgent() override;

  std::string RenderPlanMarkdown() const;

  std::string GetSystemPrompt() const override;
  std::vector<Tool> GetTools() override;

  std::vector<std::string> GetActiveTools() const override;
  std::vector<std::string> GetActiveSkills() const override;

  void Input(const Message& msg);

private:
  void TryStartRequest();

  void SendUserBatchRequest();
  void SendToolReplyRequest();

  void TrimHistory();

  void OnToolCalls(std::vector<LlmToolCall> tool_calls);
  void OnRequestDone();

  void ExecuteToolCalls(std::vector<LlmToolCall> tool_calls);

  void OnToken(const std::string& tok);

  std::vector<LlmMessage> llm_history_;
  size_t max_history_messages_{40};

  bool in_flight_{false};
  bool had_tool_calls_{false};
  size_t pending_tool_call_count_{0};

  std::string out_buf_;

  std::string model_;

  std::deque<Message> queue_;

  agent::plan2::PlanModel plan2_;
};

} // namespace agent
