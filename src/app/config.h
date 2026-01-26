#pragma once

#include <filesystem>
#include <string>

namespace agent {

struct OpenAIConfig {
  std::string base_url;
  std::string api_key; // may be resolved from ENV:XXX
  std::string model;
};

struct ShellConfig {
  bool enabled{true};
  int timeout_ms{60000};
};

struct DebugConfig {
  bool log_llm{false};
};

struct LlmConfig {
  std::filesystem::path providers_json_path{"~/.cpp-agent/llm.json"};
  std::string model;
};

struct AppConfig {
  LlmConfig llm;
  std::filesystem::path project_root{"."};
  std::filesystem::path storage_dir{"~/.micode-cpp"};
  std::filesystem::path plan_prompt_path{"config/plan_prompt.md"};
  ShellConfig shell;
  DebugConfig debug;
};

AppConfig* load_config(const std::filesystem::path& path);

} // namespace agent
