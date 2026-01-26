#include "infra/llm/llm_config_loader.h"

#include "infra/llm/llm_provider_factory.h"

#include "infra/llm/file_io.h"
#include "infra/llm/json_min.h"

#include <string>
#include <vector>

namespace agent {

bool RegisterProvidersFromConfig(LlmContext& ctx, const std::filesystem::path& path) {
  std::string json;
  if (!ReadAll(path, &json)) return false;

  std::string providers_arr;
  if (!extract_top_level_array(json, "providers", &providers_arr)) return false;

  std::vector<std::string> provider_objs;
  if (!split_top_level_objects(providers_arr, &provider_objs)) return false;

  for (const auto& obj : provider_objs) {
    std::string provider_type;
    if (!extract_string_field(obj, "type", &provider_type)) return false;

    std::string provider_name;
    if (!extract_string_field(obj, "name", &provider_name)) {
      // Back-compat: allow omitting "name" and use type as name.
      provider_name = provider_type;
    }

    std::string models_raw;
    if (!extract_raw_field(obj, "models", &models_raw)) return false;

    std::vector<std::string> models;
    if (!parse_string_array(models_raw, &models)) return false;

    std::string params_json;
    if (!extract_raw_field(obj, "params", &params_json)) {
      // Back-compat: allow provider object to directly act as params.
      params_json = obj;
    }

    const auto* factory = ctx.FindProviderFactory(provider_type);
    if (!factory) return false;

    auto provider = factory->CreateFromConfig(provider_name, std::move(models), std::move(params_json));
    if (!provider) return false;
    ctx.Register(std::move(provider));
  }

  return true;
}

} // namespace agent
