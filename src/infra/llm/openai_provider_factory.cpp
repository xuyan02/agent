#include "infra/llm/openai_provider_factory.h"

#include "infra/llm/env.h"
#include "infra/llm/json_min.h"
#include "infra/llm/openai_provider.h"

#include <string>
#include <utility>
#include <vector>

namespace agent {

std::unique_ptr<LlmProvider> OpenAIProviderFactory::CreateFromConfig(
    std::string provider_name,
    std::vector<std::string> models,
    std::string params_json) const {
  std::string base_url_raw;
  std::string api_key_raw;
  if (!extract_string_field(params_json, "base_url", &base_url_raw)) return nullptr;
  if (!extract_string_field(params_json, "api_key", &api_key_raw)) return nullptr;

  auto base_url = ResolveEnvValue(std::move(base_url_raw));
  auto api_key = ResolveEnvValue(std::move(api_key_raw));
  if (base_url.empty() || api_key.empty()) return nullptr;

  return std::make_unique<OpenAIProvider>(std::move(provider_name), std::move(models),
                                         std::move(base_url), std::move(api_key));
}

} // namespace agent
