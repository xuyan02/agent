#include "infra/llm/llm_config_loader.h"

#include "infra/llm/llm_provider_factory.h"

#include "infra/llm/file_io.h"
#include "infra/json/json.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace agent {

bool RegisterProvidersFromConfig(LlmContext& ctx, const std::filesystem::path& path) {
  std::string json_text;
  if (!ReadAll(path, &json_text)) return false;

  auto root_opt = agent::json::Parse(json_text);
  if (!root_opt) return false;
  const auto& root = *root_opt;

  if (!root.is_object()) return false;
  auto it = root.find("providers");
  if (it == root.end() || !it->is_array()) return false;

  for (const auto& obj : *it) {
    if (!obj.is_object()) return false;

    auto provider_type = agent::json::GetString(obj, "type");
    if (!provider_type) return false;

    auto provider_name = agent::json::GetStringAllowMissing(obj, "name");
    if (!provider_name.has_value()) return false;
    std::string name = provider_name.value_or(*provider_type);

    auto mit = obj.find("models");
    if (mit == obj.end() || !mit->is_array()) return false;
    std::vector<std::string> models;
    models.reserve(mit->size());
    for (const auto& m : *mit) {
      if (!m.is_string()) return false;
      models.push_back(m.get<std::string>());
    }

    std::string params_json;
    auto pit = obj.find("params");
    if (pit != obj.end()) {
      params_json = agent::json::Dump(*pit);
    } else {
      // Back-compat: allow provider object to directly act as params.
      params_json = agent::json::Dump(obj);
    }

    const auto* factory = ctx.FindProviderFactory(*provider_type);
    if (!factory) return false;

    auto provider = factory->CreateFromConfig(name, std::move(models), std::move(params_json));
    if (!provider) return false;
    ctx.Register(std::move(provider));
  }

  return true;
}

} // namespace agent
