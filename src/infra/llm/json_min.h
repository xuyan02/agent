#pragma once

#include <string>
#include <vector>

namespace agent {

bool extract_string_field(const std::string& obj, const std::string& key, std::string* out);
bool extract_raw_field(const std::string& obj, const std::string& key, std::string* out);

bool extract_top_level_array(const std::string& json, const std::string& key, std::string* out);

bool split_top_level_objects(const std::string& arr, std::vector<std::string>* out);

bool parse_string_array(const std::string& arr, std::vector<std::string>* out);

} // namespace agent
