#include "llm/openai_provider_factory.h"

#include "llm/env.h"
#include "json/json.h"
#include "llm/openai_provider.h"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

namespace agent {

std::unique_ptr<LlmProvider> OpenAIProviderFactory::CreateFromConfig(
    std::string provider_name,
    std::vector<std::string> models,
    std::string params_json) const {
  auto params_opt = agent::json::Parse(params_json);
  if (!params_opt) return nullptr;
  const auto& params = *params_opt;

  auto base_url_raw = agent::json::GetString(params, "base_url");
  auto api_key_raw = agent::json::GetString(params, "api_key");
  if (!base_url_raw || !api_key_raw) return nullptr;

  auto base_url = ResolveEnvValue(std::move(*base_url_raw));
  auto api_key = ResolveEnvValue(std::move(*api_key_raw));
  if (base_url.empty() || api_key.empty()) return nullptr;

  return std::make_unique<OpenAIProvider>(std::move(provider_name), std::move(models),
                                         std::move(base_url), std::move(api_key));
}

} // namespace agent
