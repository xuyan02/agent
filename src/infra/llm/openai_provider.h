#pragma once

#include "infra/llm/llm_provider.h"

#include "infra/http_async/async_client.h"
#include "infra/llm/sse_parser.h"

#include <memory>
#include <string>
#include <vector>

namespace agent {

class OpenAIRequest final : public LlmRequest {
public:
  OpenAIRequest(std::string base_url,
                std::string api_key,
                std::string model_name,
                std::string system_prompt,
                std::string user_prompt,
                OnToken on_token,
                OnDone on_done);

  ~OpenAIRequest() override;

private:
  bool HandleSseDataLine(const std::string& data_line);

  http::AsyncClient http_;

  http::Call call_;
  SseParser sse_;

  std::string base_url_;
  std::string api_key_;
  std::string model_name_;

  OnToken on_token_;
  OnDone on_done_;
};

class OpenAIProvider final : public LlmProvider {
public:
  OpenAIProvider(std::string name,
                 std::vector<std::string> models,
                 std::string base_url,
                 std::string api_key);

  std::string Name() const override { return name_; }
  bool SupportsModel(const std::string& model_name) const override;
  std::unique_ptr<LlmRequest> Create(std::string model_name,
                                     std::string system_prompt,
                                     std::string user_prompt,
                                     std::vector<agent::Tool> tools,
                                     LlmRequest::OnToken on_token,
                                     LlmRequest::OnDone on_done) override;

private:
  std::string name_;
  std::vector<std::string> models_;
  std::string base_url_;
  std::string api_key_;
};

} // namespace agent
