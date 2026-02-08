#pragma once

#include "dust/async/future.h"
#include "dust/async/result.h"
#include "dust/memory/ref_ptr.h"
#include "llm/chat_message.h"

#include <string>
#include <vector>

namespace agent {

class Tool;

class LlmSender {
 public:
  explicit LlmSender(std::string model) : model_(std::move(model)) {}
  virtual ~LlmSender() = default;

  LlmSender(const LlmSender&) = delete;
  LlmSender& operator=(const LlmSender&) = delete;

  // Sends a single request.
  //
  // The returned ChatMessage is expected to be an assistant ToolCalls message
  // (Text streaming is currently disabled).
  virtual dust::FuturePtr<dust::Result<dust::RefPtr<ChatMessage>, std::string>> Send(
      std::vector<dust::RefPtr<ChatMessage>> messages,
      std::vector<Tool*> tools) = 0;

 protected:
  const std::string& model() const { return model_; }

 private:
  std::string model_;
};

}  // namespace agent
