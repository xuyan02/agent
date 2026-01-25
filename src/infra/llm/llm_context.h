#pragma once

#include "infra/llm/llm_provider.h"
#include "infra/llm/llm_provider_factory.h"

#include <memory>
#include <string>
#include <vector>

namespace cpp_agent::infra::llm {

class LlmContext {
public:
  LlmContext() = default;

  // Registers a provider. Multiple providers may share the same name.
  // Provider ordering matters: Create() picks the first provider that supports
  // the given model.
  void Register(std::unique_ptr<LlmProvider> provider);

  // Registers a factory that can build providers for a given provider name.
  // Factory ordering matters: config loading uses the first matching factory.
  void RegisterFactory(std::unique_ptr<LlmProviderFactory> factory);

  // Creates a request for the given model. Returns nullptr if no provider
  // supports the model.
  std::unique_ptr<LlmRequest> Create(std::string model_name, std::string prompt);

  const LlmProviderFactory* FindProviderFactory(const std::string& provider_name) const;

  void Clear();

private:
  std::vector<std::unique_ptr<LlmProvider>> providers_;
  std::vector<std::unique_ptr<LlmProviderFactory>> factories_;
};

} // namespace cpp_agent::infra::llm
