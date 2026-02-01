#pragma once

#include "llm/llm_provider.h"
#include "llm/llm_provider_factory.h"
#include "tool/tool.h"

#include <memory>
#include <string>
#include <vector>

namespace agent {

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
  std::unique_ptr<LlmRequest> Create(std::string model_name,
                                     std::vector<LlmMessage> messages,
                                     std::vector<agent::Tool*> tools,
                                     LlmRequest::OnToken on_token,
                                     LlmRequest::OnToolCalls on_tool_calls,
                                     LlmRequest::OnDone on_done);

  const LlmProviderFactory* FindProviderFactory(const std::string& provider_name) const;

  void Clear();

private:
  std::vector<std::unique_ptr<LlmProvider>> providers_;
  std::vector<std::unique_ptr<LlmProviderFactory>> factories_;
};

} // namespace agent
