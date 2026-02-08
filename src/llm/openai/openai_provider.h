#pragma once

#include "llm/llm_provider.h"

#include <memory>
#include <string>
#include <vector>

namespace agent {

class OpenAiProvider final : public LlmProvider {
 public:
  OpenAiProvider(std::string base_url, std::string api_key);
  ~OpenAiProvider() override;

  OpenAiProvider(const OpenAiProvider&) = delete;
  OpenAiProvider& operator=(const OpenAiProvider&) = delete;

  std::unique_ptr<LlmSender> CreateSender(std::string model_name) override;

 private:
  std::string base_url_;
  std::string api_key_;
};

}  // namespace agent
