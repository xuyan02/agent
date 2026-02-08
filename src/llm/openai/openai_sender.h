#pragma once

#include "http/http_client.h"
#include "llm/chat_message.h"
#include "llm/llm_sender.h"
#include "tool/tool.h"

#include "dust/async/future.h"
#include "dust/async/result.h"
#include "dust/memory/ref_ptr.h"

#include <string>
#include <vector>

namespace agent {

class OpenAiSender final : public LlmSender {
 public:
  OpenAiSender(std::string base_url, std::string api_key, std::string model);
  ~OpenAiSender() override;

  OpenAiSender(const OpenAiSender&) = delete;
  OpenAiSender& operator=(const OpenAiSender&) = delete;

  dust::FuturePtr<dust::Result<dust::RefPtr<ChatMessage>, std::string>> Send(
      std::vector<dust::RefPtr<ChatMessage>> messages,
      std::vector<Tool*> tools) override;

 private:
  std::string base_url_;
  std::string api_key_;
};

}  // namespace agent
