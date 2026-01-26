#include "app/config.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace agent {

static bool ReadAll(const std::filesystem::path& path, std::string* out) {
  std::ifstream ifs(path);
  if (!ifs) return false;
  std::ostringstream oss;
  oss << ifs.rdbuf();
  *out = oss.str();
  return true;
}

static std::string trim(std::string s) {
  auto is_ws = [](unsigned char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; };
  while (!s.empty() && is_ws(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && is_ws(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

// Minimal JSON extraction without adding a JSON dependency.
// This expects the example format and is intentionally strict.
static bool ExtractJsonString(const std::string& json, const std::string& key, std::string* out) {
  auto pos = json.find('"' + key + '"');
  if (pos == std::string::npos) return false;

  pos = json.find(':', pos);
  if (pos == std::string::npos) return false;

  pos++;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) pos++;
  if (pos >= json.size() || json[pos] != '"') return false;

  pos++;
  out->clear();
  for (; pos < json.size(); ++pos) {
    char c = json[pos];
    if (c == '\\') {
      if (pos + 1 >= json.size()) break;
      out->push_back(json[pos + 1]);
      pos++;
      continue;
    }
    if (c == '"') return true;
    out->push_back(c);
  }
  return false;
}

static std::string extract_json_string_or_default(const std::string& json,
                                                  const std::string& key,
                                                  const std::string& def) {
  auto pos = json.find('"' + key + '"');
  if (pos == std::string::npos) return def;
  std::string out;
  if (!ExtractJsonString(json, key, &out)) return def;
  return out;
}

static bool extract_json_bool_or_default(const std::string& json, const std::string& key, bool def) {
  auto pos = json.find('"' + key + '"');
  if (pos == std::string::npos) return def;
  pos = json.find(':', pos);
  if (pos == std::string::npos) return def;
  pos++;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) pos++;
  if (json.compare(pos, 4, "true") == 0) return true;
  if (json.compare(pos, 5, "false") == 0) return false;
  return def;
}

static int extract_json_int_or_default(const std::string& json, const std::string& key, int def) {
  auto pos = json.find('"' + key + '"');
  if (pos == std::string::npos) return def;
  pos = json.find(':', pos);
  if (pos == std::string::npos) return def;
  pos++;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) pos++;
  std::string num;
  while (pos < json.size() && (json[pos] == '-' || (json[pos] >= '0' && json[pos] <= '9'))) {
    num.push_back(json[pos++]);
  }

  if (num.empty()) return def;

  // Avoid std::stoi (throws on errors).
  int sign = 1;
  size_t i = 0;
  if (num[0] == '-') {
    sign = -1;
    i = 1;
  }
  int v = 0;
  for (; i < num.size(); ++i) {
    const char c = num[i];
    if (c < '0' || c > '9') return def;
    v = v * 10 + (c - '0');
  }
  return sign * v;
}

static std::string resolve_env_value(std::string v) {
  v = trim(std::move(v));
  constexpr const char* kPrefix = "ENV:";
  if (v.rfind(kPrefix, 0) == 0) {
    auto env = v.substr(std::string(kPrefix).size());
    const char* val = std::getenv(env.c_str());
    return val ? std::string(val) : std::string();
  }
  return v;
}

static std::filesystem::path expand_user_home(std::filesystem::path p) {
  // Support common "~" usage on Unix-like systems.
  const auto s = p.string();
  if (s == "~" || s.rfind("~/", 0) == 0) {
    const char* home = std::getenv("HOME");
    if (home && *home) {
      if (s == "~") return std::filesystem::path(home);
      return std::filesystem::path(home) / s.substr(2);
    }
  }
  return p;
}

AppConfig* load_config(const std::filesystem::path& path) {
  static AppConfig cfg;
  std::string json;
  if (!ReadAll(expand_user_home(path), &json)) return nullptr;

  cfg = AppConfig{};
  cfg.llm.providers_json_path = expand_user_home(resolve_env_value(
      extract_json_string_or_default(json, "providers_json_path", "~/.cpp-agent/llm.json")));

  std::string model;
  if (!ExtractJsonString(json, "model", &model)) return nullptr;
  cfg.llm.model = resolve_env_value(std::move(model));

  std::string project_root;
  if (!ExtractJsonString(json, "project_root", &project_root)) return nullptr;
  cfg.project_root = expand_user_home(resolve_env_value(std::move(project_root)));

  std::string storage_dir;
  if (!ExtractJsonString(json, "storage_dir", &storage_dir)) return nullptr;
  cfg.storage_dir = expand_user_home(resolve_env_value(std::move(storage_dir)));

  cfg.plan_prompt_path =
      resolve_env_value(extract_json_string_or_default(json, "plan_prompt_path", "config/plan_prompt.md"));
  cfg.plan_prompt_path = expand_user_home(cfg.plan_prompt_path);

  cfg.shell.enabled = extract_json_bool_or_default(json, "enabled", true);
  cfg.shell.timeout_ms = extract_json_int_or_default(json, "timeout_ms", 60000);

  cfg.debug.log_llm = extract_json_bool_or_default(json, "log_llm", false);

  return &cfg;
}

} // namespace agent
