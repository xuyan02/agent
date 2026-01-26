#pragma once

#include "infra/llm/llm_provider_factory.h"

namespace agent {

class OpenAIProviderFactory final : public LlmProviderFactory {
public:
  std::string Name() const override { return "openai"; }

  std::unique_ptr<LlmProvider> CreateFromConfig(std::string provider_name,
                                                std::vector<std::string> models,
                                                std::string params_json) const override;
};

} // namespace agent
