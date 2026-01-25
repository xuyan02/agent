#include "infra/llm/llm_context.h"

namespace cpp_agent::infra::llm {

void LlmContext::Register(std::unique_ptr<LlmProvider> provider) {
  if (!provider) return;
  providers_.push_back(std::move(provider));
}

void LlmContext::RegisterFactory(std::unique_ptr<LlmProviderFactory> factory) {
  if (!factory) return;
  factories_.push_back(std::move(factory));
}

const LlmProviderFactory* LlmContext::FindProviderFactory(const std::string& provider_name) const {
  for (const auto& f : factories_) {
    if (!f) continue;
    if (f->name() == provider_name) return f.get();
  }
  return nullptr;
}

std::unique_ptr<LlmRequest> LlmContext::Create(std::string model_name,
                                              std::string prompt) {
  for (auto& provider : providers_) {
    if (!provider) continue;
    if (!provider->SupportsModel(model_name)) continue;
    return provider->Create(std::move(model_name), std::move(prompt));
  }
  return nullptr;
}

void LlmContext::Clear() {
  providers_.clear();
  factories_.clear();
}

} // namespace cpp_agent::infra::llm
