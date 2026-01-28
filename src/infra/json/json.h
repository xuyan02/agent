#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace agent::json {

// Parse JSON with exceptions disabled (nlohmann/json is configured with JSON_NoExceptions).
// Returns std::nullopt on parse error.
std::optional<nlohmann::json> Parse(const std::string& s);

// NOTE: Call sites that use these helpers must include <nlohmann/json.hpp>
// (json_fwd.hpp only forward-declares the type).

std::optional<std::string> GetString(const nlohmann::json& obj, const char* key);
std::optional<std::string> GetStringAllowMissing(const nlohmann::json& obj, const char* key);

std::optional<std::vector<std::string>> GetStringArrayAllowMissing(const nlohmann::json& obj,
                                                                   const char* key);

// Dump JSON string with stable formatting suitable for API requests.
std::string Dump(const nlohmann::json& j);

} // namespace agent::json
