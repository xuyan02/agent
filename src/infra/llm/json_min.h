#pragma once

#include <string>
#include <vector>

namespace agent {

bool extract_string_field(const std::string& obj, const std::string& key, std::string* out);
bool extract_raw_field(const std::string& obj, const std::string& key, std::string* out);

bool extract_top_level_array(const std::string& json, const std::string& key, std::string* out);

bool split_top_level_objects(const std::string& arr, std::vector<std::string>* out);

bool parse_string_array(const std::string& arr, std::vector<std::string>* out);

// Optional helper: extract key as a JSON string array (e.g. {"tools":["a","b"]}).
// Returns false if key is missing or parsing fails.
bool extract_string_array_field(const std::string& obj,
                               const std::string& key,
                               std::vector<std::string>* out);

} // namespace agent
