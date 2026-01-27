#pragma once

#include "infra/llm/llm_request.h"

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

protected:
  Team& team();
  const Team& team() const;

  Runtime& runtime();
  const Runtime& runtime() const;

  bool HasActiveRequest() const;

  bool StartLlmRequest(std::string model,
                       std::string system_prompt,
                       std::string user_prompt,
                       agent::LlmRequest::OnToken on_token,
                       agent::LlmRequest::OnDone on_done);

  void CancelActiveRequest();

private:
  Team& team_;
  std::string name_;

  std::unique_ptr<agent::LlmRequest> active_req_;
};

} // namespace agent
