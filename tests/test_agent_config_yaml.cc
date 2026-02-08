#include "config/agent_config.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace {

std::filesystem::path WriteTemp(const std::string& content) {
  std::filesystem::path p = std::filesystem::temp_directory_path() /
                            ("cpp-agent-agent-yaml-" + std::to_string(std::rand()) + ".yaml");
  std::ofstream out(p);
  out << content;
  out.close();
  return p;
}

}  // namespace

int main() {
  {
    auto p = WriteTemp("model: gpt-4.1\nopenai:\n  api_key: sk-test\n  base_url: https://api.openai.com\n");
    std::string err;
    std::optional<agent::AgentConfig> cfg = agent::LoadAgentConfigYaml(p.string(), &err);
    assert(cfg);
    assert(cfg->model == "gpt-4.1");
    assert(cfg->openai);
    assert(cfg->openai->api_key == "sk-test");
    assert(cfg->openai->base_url == "https://api.openai.com");
    std::error_code ec;
    std::filesystem::remove(p, ec);
  }

  {
    auto p = WriteTemp("model: m\nopenai:\n  base_url: https://api.openai.com\n");
    std::string err;
    std::optional<agent::AgentConfig> cfg = agent::LoadAgentConfigYaml(p.string(), &err);
    assert(!cfg);
    assert(err.find("api_key") != std::string::npos);
    std::error_code ec;
    std::filesystem::remove(p, ec);
  }

  {
    auto p = WriteTemp("openai:\n  api_key: x\n");
    std::string err;
    std::optional<agent::AgentConfig> cfg = agent::LoadAgentConfigYaml(p.string(), &err);
    assert(!cfg);
    assert(err.find("model") != std::string::npos);
    std::error_code ec;
    std::filesystem::remove(p, ec);
  }

  return 0;
}
