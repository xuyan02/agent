#pragma once

#include "interfaces/illm_client.h"

#include <string>

namespace agent {

class OpenAIClient final : public agent::ILlmClient {
public:
  OpenAIClient(std::string base_url, std::string api_key);

  agent::LlmResponse Complete(
      const std::vector<agent::LlmMessage>& messages,
      const agent::LlmOptions& options) override;

  void set_tools_json(std::string tools_json);
  void set_log_requests(bool enabled) { log_requests_ = enabled; }

private:
  std::string base_url_;
  std::string api_key_;
  std::string tools_json_; // raw JSON array
  bool log_requests_{false};
};

} // namespace agent
