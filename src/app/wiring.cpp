#include "app/wiring.h"

#include "infra/llm/llm_config_loader.h"
#include "infra/llm/openai_provider_factory.h"

#include <memory>

namespace agent {

std::unique_ptr<agent::LlmContext> build_llm(const AppConfig& cfg) {
  auto llm = std::make_unique<agent::LlmContext>();
  llm->RegisterFactory(std::make_unique<agent::OpenAIProviderFactory>());

  if (!agent::RegisterProvidersFromConfig(*llm, cfg.llm.providers_json_path)) {
    return nullptr;
  }

  return llm;
}

} // namespace agent
