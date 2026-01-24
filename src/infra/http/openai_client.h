#pragma once

#include "interfaces/illm_client.h"

#include <string>

namespace cpp_agent::infra::http {

class OpenAIClient final : public cpp_agent::interfaces::ILlmClient {
public:
  OpenAIClient(std::string base_url, std::string api_key);

  cpp_agent::interfaces::LlmResponse complete(
      const std::vector<cpp_agent::core::Message>& messages,
      const cpp_agent::interfaces::LlmOptions& options) override;

  void set_tools_json(std::string tools_json);
  void set_log_requests(bool enabled) { log_requests_ = enabled; }

private:
  std::string base_url_;
  std::string api_key_;
  std::string tools_json_; // raw JSON array
  bool log_requests_{false};
};

} // namespace cpp_agent::infra::http
