#include "infra/llm/llm_config_loader.h"

#include "infra/llm/llm_provider_factory.h"

#include "infra/llm/file_io.h"
#include "infra/llm/json_min.h"

#include <string>
#include <vector>

namespace cpp_agent::infra::llm {

bool RegisterProvidersFromConfig(LlmContext& ctx, const std::filesystem::path& path) {
  std::string json;
  if (!ReadAll(path, &json)) return false;

  std::string providers_arr;
  if (!json_min::extract_top_level_array(json, "providers", &providers_arr)) return false;

  std::vector<std::string> provider_objs;
  if (!json_min::split_top_level_objects(providers_arr, &provider_objs)) return false;

  for (const auto& obj : provider_objs) {
    std::string provider_name;
    if (!json_min::extract_string_field(obj, "name", &provider_name)) return false;

    std::string models_raw;
    if (!json_min::extract_raw_field(obj, "models", &models_raw)) return false;

    std::vector<std::string> models;
    if (!json_min::parse_string_array(models_raw, &models)) return false;

    std::string params_json;
    if (!json_min::extract_raw_field(obj, "params", &params_json)) return false;

    const auto* factory = ctx.FindProviderFactory(provider_name);
    if (!factory) return false;

    auto provider = factory->CreateFromConfig(provider_name, std::move(models), std::move(params_json));
    if (!provider) return false;
    ctx.Register(std::move(provider));
  }

  return true;
}

} // namespace cpp_agent::infra::llm
