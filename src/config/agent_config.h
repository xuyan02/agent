#pragma once

#include <optional>
#include <string>

namespace agent {

struct OpenAiProviderConfig {
  std::string base_url;
  std::string api_key;
};

struct AgentConfig {
  std::string model;
  std::optional<OpenAiProviderConfig> openai;
};

// Reads {agent_dir}/agent.yaml.
// Returns std::nullopt on failure; error_out (if non-null) receives a message.
std::optional<AgentConfig> LoadAgentConfigYaml(const std::string& path,
                                               std::string* error_out);

}  // namespace agent
