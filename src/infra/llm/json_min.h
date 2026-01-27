#pragma once

#include <string>
#include <vector>

namespace agent {

bool extract_string_field(const std::string& obj, const std::string& key, std::string* out);
bool extract_raw_field(const std::string& obj, const std::string& key, std::string* out);

bool extract_top_level_array(const std::string& json, const std::string& key, std::string* out);

bool split_top_level_objects(const std::string& arr, std::vector<std::string>* out);

// Helpers used by streaming parsing.
size_t skip_ws(const std::string& s, size_t i);
size_t skip_json_object(const std::string& s, size_t i);
std::string extract_json_string_or_empty(const std::string& obj, const std::string& key);

// Decode JSON string body (not including surrounding quotes). Best-effort; supports common escapes.
std::string json_unescape(const std::string& s);

bool parse_string_array(const std::string& arr, std::vector<std::string>* out);

// Optional helper: extract key as a JSON string array (e.g. {"tools":["a","b"]}).
// Returns false if key is missing or parsing fails.
bool extract_string_array_field(const std::string& obj,
                               const std::string& key,
                               std::vector<std::string>* out);

} // namespace agent
