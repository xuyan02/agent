#include "app/config.h"

#include "infra/json/json.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <limits>
#include <optional>
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

static std::string get_string_or_default(const nlohmann::json& obj,
                                         const char* key,
                                         const std::string& def) {
  if (!obj.is_object())
    return def;
  auto it = obj.find(key);
  if (it == obj.end() || !it->is_string())
    return def;
  return it->get<std::string>();
}

static std::optional<std::string> get_string_required(const nlohmann::json& obj, const char* key) {
  if (!obj.is_object())
    return std::nullopt;
  auto it = obj.find(key);
  if (it == obj.end() || !it->is_string())
    return std::nullopt;
  return it->get<std::string>();
}

static bool get_bool_or_default(const nlohmann::json& obj, const char* key, bool def) {
  if (!obj.is_object())
    return def;
  auto it = obj.find(key);
  if (it == obj.end() || !it->is_boolean())
    return def;
  return it->get<bool>();
}

static int get_int_or_default(const nlohmann::json& obj, const char* key, int def) {
  if (!obj.is_object())
    return def;
  auto it = obj.find(key);
  if (it == obj.end() || !it->is_number_integer())
    return def;
  const auto v = it->get<long long>();
  if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max())
    return def;
  return static_cast<int>(v);
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

  auto root_opt = agent::json::Parse(json);
  if (!root_opt)
    return nullptr;

  const auto& root = *root_opt;

  cfg.llm.providers_json_path = expand_user_home(resolve_env_value(
      get_string_or_default(root, "providers_json_path", "~/.cpp-agent/llm.json")));

  auto model = get_string_required(root, "model");
  if (!model)
    return nullptr;
  cfg.llm.model = resolve_env_value(std::move(*model));

  auto project_root = get_string_required(root, "project_root");
  if (!project_root)
    return nullptr;
  cfg.project_root = expand_user_home(resolve_env_value(std::move(*project_root)));

  auto storage_dir = get_string_required(root, "storage_dir");
  if (!storage_dir)
    return nullptr;
  cfg.storage_dir = expand_user_home(resolve_env_value(std::move(*storage_dir)));

  cfg.plan_prompt_path = resolve_env_value(
      get_string_or_default(root, "plan_prompt_path", "config/plan_prompt.md"));
  cfg.plan_prompt_path = expand_user_home(cfg.plan_prompt_path);

  cfg.shell.enabled = get_bool_or_default(root, "enabled", true);
  cfg.shell.timeout_ms = get_int_or_default(root, "timeout_ms", 60000);

  cfg.debug.log_llm = get_bool_or_default(root, "log_llm", false);

  return &cfg;
}

}  // namespace agent
