#pragma once

#include "runtime/agent.h"

#include "dust/functional/function.h"

#include <string>

namespace agent {

class ProfessionalAgent final : public Agent {
public:
  using ReplyFn = dust::Function<void(const std::string& content)>;

  ProfessionalAgent(Team& team, std::string name, std::string model);
  ~ProfessionalAgent() override;

  void Input(const std::string& input, ReplyFn reply);

private:
  void OnToken(const std::string& tok);
  void OnDone();

  std::string model_;
  std::string out_;
  ReplyFn reply_;
};

} // namespace agent
