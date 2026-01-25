#include "app/config.h"

#include "core/errors.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace cpp_agent::app {

static std::string read_all_or_throw(const std::filesystem::path& path) {
  std::ifstream ifs(path);
  if (!ifs) {
    throw cpp_agent::core::AgentError(cpp_agent::core::ErrorCode::kIo,
                                     "Failed to open config: " + path.string());
  }
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

static std::string trim(std::string s) {
  auto is_ws = [](unsigned char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; };
  while (!s.empty() && is_ws(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && is_ws(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

// Minimal JSON extraction without adding a JSON dependency.
// This expects the example format and is intentionally strict.
static std::string extract_json_string_or_throw(const std::string& json,
                                               const std::string& key) {
  auto pos = json.find('"' + key + '"');
  if (pos == std::string::npos) {
    throw cpp_agent::core::AgentError(cpp_agent::core::ErrorCode::kInvalidArgument,
                                     "Config missing key: " + key);
  }
  pos = json.find(':', pos);
  if (pos == std::string::npos) throw cpp_agent::core::AgentError(cpp_agent::core::ErrorCode::kInvalidArgument,
                                                                  "Config parse error near key: " + key);
  pos++;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) pos++;
  if (pos >= json.size() || json[pos] != '"') {
    throw cpp_agent::core::AgentError(cpp_agent::core::ErrorCode::kInvalidArgument,
                                     "Config key not a string: " + key);
  }
  pos++;
  std::string out;
  for (; pos < json.size(); ++pos) {
    char c = json[pos];
    if (c == '\\') {
      if (pos + 1 >= json.size()) break;
      out.push_back(json[pos + 1]);
      pos++;
      continue;
    }
    if (c == '"') break;
    out.push_back(c);
  }
  return out;
}

static std::string extract_json_string_or_default(const std::string& json,
                                                   const std::string& key,
                                                   const std::string& def) {
  auto pos = json.find('"' + key + '"');
  if (pos == std::string::npos) return def;
  try {
    return extract_json_string_or_throw(json, key);
  } catch (...) {
    return def;
  }
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
  try {
    return num.empty() ? def : std::stoi(num);
  } catch (...) {
    return def;
  }
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

AppConfig load_config_or_throw(const std::filesystem::path& path) {
  auto json = read_all_or_throw(expand_user_home(path));

  AppConfig cfg;
  cfg.openai.base_url = resolve_env_value(extract_json_string_or_throw(json, "base_url"));
  cfg.openai.api_key = resolve_env_value(extract_json_string_or_throw(json, "api_key"));
  cfg.openai.model = resolve_env_value(extract_json_string_or_throw(json, "model"));

  cfg.project_root = expand_user_home(resolve_env_value(extract_json_string_or_throw(json, "project_root")));
  cfg.storage_dir = expand_user_home(resolve_env_value(extract_json_string_or_throw(json, "storage_dir")));

  cfg.plan_prompt_path = resolve_env_value(extract_json_string_or_default(json, "plan_prompt_path", "config/plan_prompt.md"));
  cfg.plan_prompt_path = expand_user_home(cfg.plan_prompt_path);

  cfg.shell.enabled = extract_json_bool_or_default(json, "enabled", true);
  cfg.shell.timeout_ms = extract_json_int_or_default(json, "timeout_ms", 60000);

  cfg.debug.log_llm = extract_json_bool_or_default(json, "log_llm", false);

  return cfg;
}

} // namespace cpp_agent::app
