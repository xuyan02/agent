#pragma once

#include "llm/llm_provider.h"

#include "dust/functional/closure.h"

#include "http_async/async_client.h"
#include "llm/openai_stream_accumulator.h"
#include "llm/sse_parser.h"

#include <memory>
#include <string>
#include <vector>

namespace agent {

class OpenAIRequest final : public LlmRequest {
public:
  // Provider-internal hook: emit complete tool calls at the end of a tool-call round.
  using OnToolCalls = LlmRequest::OnToolCalls;

  OpenAIRequest(std::string base_url,
                std::string api_key,
                std::string model_name,
                std::vector<LlmMessage> messages,
                std::vector<agent::Tool*> tools,
                OnToken on_token,
                OnToolCalls on_tool_calls,
                OnDone on_done);

  ~OpenAIRequest() override;

private:
  bool HandleSseDataLine(const std::string& data_line);

  std::unique_ptr<http::AsyncClient> http_;
  SseParser sse_;

  std::string base_url_;
  std::string api_key_;
  std::string model_name_;

  OnToken on_token_;
  OnToolCalls on_tool_calls_;
  OnDone on_done_;

  OpenAIStreamAccumulator acc_;
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
                                     std::vector<LlmMessage> messages,
                                     std::vector<agent::Tool*> tools,
                                     LlmRequest::OnToken on_token,
                                     LlmRequest::OnToolCalls on_tool_calls,
                                     LlmRequest::OnDone on_done) override;

private:
  std::string name_;
  std::vector<std::string> models_;
  std::string base_url_;
  std::string api_key_;
};

} // namespace agent
