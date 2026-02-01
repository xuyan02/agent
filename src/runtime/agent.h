#pragma once

#include "infra/llm/llm_message.h"
#include "infra/llm/llm_request.h"
#include "tool/tool.h"

#include <memory>
#include <string>

namespace agent {
class Runtime;
class Team;
}

namespace agent {

class Agent {
public:
  Agent(Team& team, std::string name);
  virtual ~Agent();

  std::string name() const;

  virtual std::string GetSystemPrompt() const;
  virtual std::vector<Tool> GetTools();
  virtual std::vector<std::string> GetActiveTools() const;
  virtual std::vector<std::string> GetActiveSkills() const;

protected:
  Team& team();
  const Team& team() const;

  Runtime& runtime();
  const Runtime& runtime() const;

  bool HasActiveRequest() const;

  bool StartLlmRequest(std::string model,
                       std::vector<LlmMessage> messages,
                       agent::LlmRequest::OnToken on_token,
                       agent::LlmRequest::OnToolCalls on_tool_calls,
                       agent::LlmRequest::OnDone on_done);

  void CancelActiveRequest();

private:
  Team& team_;
  std::string name_;

  std::unique_ptr<agent::LlmRequest> active_req_;
};

} // namespace agent
