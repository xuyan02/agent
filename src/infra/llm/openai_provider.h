#pragma once

#include "infra/llm/llm_provider.h"

#include "infra/http/openai_client.h"

#include <memory>
#include <string>
#include <vector>

namespace cpp_agent::infra::llm {

class OpenAIRequest final : public LlmRequest {
public:
  OpenAIRequest(std::shared_ptr<cpp_agent::infra::http::OpenAIClient> client,
                std::string model_name,
                std::string prompt);
  ~OpenAIRequest() override;

private:
  std::shared_ptr<cpp_agent::infra::http::OpenAIClient> client_;
};

class OpenAIProvider final : public LlmProvider {
public:
  OpenAIProvider(std::string name,
                 std::vector<std::string> models,
                 std::string base_url,
                 std::string api_key);

  std::string name() const override { return name_; }
  bool SupportsModel(const std::string& model_name) const override;
  std::unique_ptr<LlmRequest> Create(std::string model_name, std::string prompt) override;

private:
  std::string name_;
  std::vector<std::string> models_;
  std::string base_url_;
  std::string api_key_;
};

} // namespace cpp_agent::infra::llm
