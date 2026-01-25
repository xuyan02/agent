#pragma once

#include "infra/llm/llm_provider.h"

#include <memory>
#include <string>
#include <vector>

namespace cpp_agent::infra::llm {

// Factory that can create a provider instance for a given provider name.
//
// It is registered into LlmContext ahead of time.
//
// NOTE: Parameter parsing is provider-specific. The params_json is the raw JSON
// object string from the config file.
class LlmProviderFactory {
public:
  virtual ~LlmProviderFactory() = default;

  LlmProviderFactory(const LlmProviderFactory&) = delete;
  LlmProviderFactory& operator=(const LlmProviderFactory&) = delete;

  // Provider name this factory handles (e.g. "openai").
  virtual std::string name() const = 0;

  // Creates a provider.
  // - provider_name: matches name().
  // - models: parsed model list.
  // - params_json: raw JSON object string.
  virtual std::unique_ptr<LlmProvider> CreateFromConfig(std::string provider_name,
                                                        std::vector<std::string> models,
                                                        std::string params_json) const = 0;

protected:
  LlmProviderFactory() = default;
};

} // namespace cpp_agent::infra::llm
