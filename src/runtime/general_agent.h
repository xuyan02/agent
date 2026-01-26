#pragma once

#include "runtime/agent.h"
#include "runtime/message.h"

#include "infra/llm/llm_context.h"
#include "infra/llm/llm_request.h"

#include "dust/functional/function.h"

#include <deque>
#include <string>

namespace agent {

class GeneralAgent final : public Agent {
public:
  GeneralAgent(Runtime& runtime,
               std::string name,
               agent::LlmContext& llm,
               std::string model);
  ~GeneralAgent() override;

  void Input(const Message& msg);

private:
  void TryStartRequest();
  void OnRequestDone();

  static std::string BuildBatchInput(std::deque<Message>& q);

  void OnToken(const std::string& tok);
  void EmitParsedOutputLines(const std::string& raw);

  std::string out_buf_;
  std::string current_to_;
  std::string current_content_;

  agent::LlmContext& llm_;
  std::string model_;

  std::deque<Message> queue_;
  bool in_flight_ = false;
  std::unique_ptr<agent::LlmRequest> active_req_;
};

} // namespace agent
