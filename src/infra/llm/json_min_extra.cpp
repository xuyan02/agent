#include "infra/llm/json_min.h"

namespace agent {

size_t skip_json_object(const std::string& s, size_t i) {
  i = skip_ws(s, i);
  if (i >= s.size() || s[i] != '{') return i;

  int depth = 0;
  bool in_str = false;
  for (size_t p = i; p < s.size(); p++) {
    char c = s[p];
    if (in_str) {
      if (c == '\\') {
        if (p + 1 < s.size()) p++;
        continue;
      }
      if (c == '"') in_str = false;
      continue;
    }

    if (c == '"') {
      in_str = true;
      continue;
    }
    if (c == '{') depth++;
    if (c == '}') {
      depth--;
      if (depth == 0) return p + 1;
    }
  }
  return i;
}

std::string json_unescape(const std::string& s) {
  std::string out;
  out.reserve(s.size());

  for (size_t i = 0; i < s.size(); i++) {
    char c = s[i];
    if (c != '\\') {
      out.push_back(c);
      continue;
    }

    if (i + 1 >= s.size()) break;
    char e = s[++i];
    switch (e) {
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case '\\': out.push_back('\\'); break;
      case '"': out.push_back('"'); break;
      default:
        // Best-effort: keep unknown escapes as-is.
        out.push_back(e);
        break;
    }
  }

  return out;
}

std::string extract_json_string_or_empty(const std::string& obj, const std::string& key) {
  std::string raw;
  if (!extract_raw_field(obj, key, &raw)) return {};
  size_t i = 0;
  i = skip_ws(raw, i);
  if (i >= raw.size() || raw[i] != '"') return {};
  i++;
  std::string out;
  for (; i < raw.size(); i++) {
    char c = raw[i];
    if (c == '"') break;
    if (c == '\\') {
      if (i + 1 >= raw.size()) break;
      char e = raw[++i];
      out.push_back('\\');
      out.push_back(e);
      continue;
    }
    out.push_back(c);
  }
  return json_unescape(out);
}

} // namespace agent
