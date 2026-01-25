#pragma once

#include "infra/llm/llm_provider_factory.h"

namespace cpp_agent::infra::llm {

class OpenAIProviderFactory final : public LlmProviderFactory {
public:
  std::string name() const override { return "openai"; }

  std::unique_ptr<LlmProvider> CreateFromConfig(std::string provider_name,
                                                std::vector<std::string> models,
                                                std::string params_json) const override;
};

} // namespace cpp_agent::infra::llm
