#pragma once

#include <filesystem>
#include <string>

namespace cpp_agent::app {

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

struct AppConfig {
  OpenAIConfig openai;
  std::filesystem::path project_root{"."};
  std::filesystem::path storage_dir{"~/.micode-cpp"};
  ShellConfig shell;
  DebugConfig debug;
};

AppConfig load_config_or_throw(const std::filesystem::path& path);

} // namespace cpp_agent::app
