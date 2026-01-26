#include "infra/llm/json_min.h"

namespace agent {
namespace {

size_t skip_ws(const std::string& s, size_t i) {
  while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r')) i++;
  return i;
}

} // namespace

bool extract_top_level_array(const std::string& json, const std::string& key, std::string* out) {
  if (!out) return false;
  const auto needle = '"' + key + '"';
  auto pos = json.find(needle);
  if (pos == std::string::npos) return false;
  pos = json.find(':', pos + needle.size());
  if (pos == std::string::npos) return false;
  pos = skip_ws(json, pos + 1);
  if (pos >= json.size() || json[pos] != '[') return false;

  int depth = 0;
  size_t start = pos;
  for (size_t i = pos; i < json.size(); i++) {
    char c = json[i];
    if (c == '[') depth++;
    if (c == ']') {
      depth--;
      if (depth == 0) {
        *out = json.substr(start, i - start + 1);
        return true;
      }
    }
  }
  return false;
}

bool extract_string_field(const std::string& obj, const std::string& key, std::string* out) {
  if (!out) return false;
  const auto needle = '"' + key + '"';
  auto pos = obj.find(needle);
  if (pos == std::string::npos) return false;
  pos = obj.find(':', pos + needle.size());
  if (pos == std::string::npos) return false;
  pos = skip_ws(obj, pos + 1);
  if (pos >= obj.size() || obj[pos] != '"') return false;

  std::string s;
  for (size_t i = pos + 1; i < obj.size(); i++) {
    char c = obj[i];
    if (c == '"') {
      *out = std::move(s);
      return true;
    }
    if (c == '\\') {
      if (i + 1 >= obj.size()) return false;
      char n = obj[++i];
      switch (n) {
        case '"': s.push_back('"'); break;
        case '\\': s.push_back('\\'); break;
        case 'n': s.push_back('\n'); break;
        case 'r': s.push_back('\r'); break;
        case 't': s.push_back('\t'); break;
        default: s.push_back(n); break;
      }
      continue;
    }
    s.push_back(c);
  }
  return false;
}

bool extract_raw_field(const std::string& obj, const std::string& key, std::string* out) {
  if (!out) return false;
  const auto needle = '"' + key + '"';
  auto pos = obj.find(needle);
  if (pos == std::string::npos) return false;
  pos = obj.find(':', pos + needle.size());
  if (pos == std::string::npos) return false;
  pos = skip_ws(obj, pos + 1);
  if (pos >= obj.size()) return false;

  const char open = obj[pos];
  if (open != '[' && open != '{') return false;
  const char close = (open == '[') ? ']' : '}';

  int depth = 0;
  size_t start = pos;
  for (size_t i = pos; i < obj.size(); i++) {
    char c = obj[i];
    if (c == open) depth++;
    if (c == close) {
      depth--;
      if (depth == 0) {
        *out = obj.substr(start, i - start + 1);
        return true;
      }
    }
  }
  return false;
}

bool split_top_level_objects(const std::string& arr, std::vector<std::string>* out) {
  if (!out) return false;
  out->clear();

  size_t i = 0;
  i = skip_ws(arr, i);
  if (i >= arr.size() || arr[i] != '[') return false;
  i++;

  for (;;) {
    i = skip_ws(arr, i);
    if (i >= arr.size()) return false;
    if (arr[i] == ']') break;
    if (arr[i] == ',') {
      i++;
      continue;
    }
    if (arr[i] != '{') return false;

    int depth = 0;
    size_t start = i;
    for (; i < arr.size(); i++) {
      char c = arr[i];
      if (c == '{') depth++;
      if (c == '}') {
        depth--;
        if (depth == 0) {
          out->push_back(arr.substr(start, i - start + 1));
          i++;
          break;
        }
      }
    }
    if (depth != 0) return false;
  }

  return true;
}

bool parse_string_array(const std::string& arr, std::vector<std::string>* out) {
  if (!out) return false;
  out->clear();

  size_t i = 0;
  i = skip_ws(arr, i);
  if (i >= arr.size() || arr[i] != '[') return false;
  i++;

  for (;;) {
    i = skip_ws(arr, i);
    if (i >= arr.size()) return false;
    if (arr[i] == ']') break;
    if (arr[i] == ',') {
      i++;
      continue;
    }
    if (arr[i] != '"') return false;

    std::string s;
    for (size_t j = i + 1; j < arr.size(); j++) {
      char c = arr[j];
      if (c == '"') {
        out->push_back(std::move(s));
        i = j + 1;
        break;
      }
      if (c == '\\') {
        if (j + 1 >= arr.size()) return false;
        char n = arr[++j];
        s.push_back(n);
        continue;
      }
      s.push_back(c);
    }
  }

  return true;
}

} // namespace agent
