#include "infra/json/json.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace agent::json {

std::optional<nlohmann::json> Parse(const std::string& s) {
  // With JSON_NoExceptions enabled, parse returns a "discarded" json value on error.
  auto j = nlohmann::json::parse(s, nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded()) return std::nullopt;
  return j;
}

std::optional<std::string> GetString(const nlohmann::json& obj, const char* key) {
  if (!obj.is_object()) return std::nullopt;
  auto it = obj.find(key);
  if (it == obj.end() || !it->is_string()) return std::nullopt;
  return it->get<std::string>();
}

std::optional<std::string> GetStringAllowMissing(const nlohmann::json& obj, const char* key) {
  if (!obj.is_object()) return std::nullopt;
  auto it = obj.find(key);
  if (it == obj.end()) return std::optional<std::string>{};
  if (!it->is_string()) return std::nullopt;
  return it->get<std::string>();
}

std::optional<std::vector<std::string>> GetStringArrayAllowMissing(const nlohmann::json& obj,
                                                                   const char* key) {
  if (!obj.is_object()) return std::nullopt;
  auto it = obj.find(key);
  if (it == obj.end()) return std::optional<std::vector<std::string>>{};
  if (!it->is_array()) return std::nullopt;

  std::vector<std::string> out;
  out.reserve(it->size());
  for (const auto& v : *it) {
    if (!v.is_string()) return std::nullopt;
    out.push_back(v.get<std::string>());
  }
  return out;
}

std::string Dump(const nlohmann::json& j) {
  return j.dump();
}

} // namespace agent::json
