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

  std::string GetSystemPrompt() const override;
  std::vector<Tool> GetTools() override;

  std::vector<std::string> GetActiveTools() const override;
  std::vector<std::string> GetActiveSkills() const override;

  void Input(const Message& msg);

private:
  enum class RoundMode {
    kUnknown,
    kAssistantText,
    kToolCall,
  };

  void TryStartRequest();
  void StartRound(std::vector<LlmMessage> msgs);
  void TrimHistory();

  void OnToolCalls(std::vector<LlmToolCall> tool_calls);
  void OnRequestDone();

  void ExecuteToolCalls(std::vector<LlmToolCall> tool_calls);
  void ExecuteNextToolCall();

  void OnToken(const std::string& tok);

  std::vector<LlmMessage> llm_history_;
  size_t max_history_messages_{40};
  RoundMode round_mode_{RoundMode::kUnknown};

  std::vector<LlmToolCall> pending_tool_calls_;
  size_t pending_tool_call_idx_{0};

  std::string out_buf_;

  std::string model_;

  std::deque<Message> queue_;

  agent::plan2::PlanModel plan2_;
};

} // namespace agent
