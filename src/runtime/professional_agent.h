#pragma once

#include "runtime/agent.h"

#include "dust/functional/function.h"

#include <string>

namespace agent {

class ProfessionalAgent final : public Agent {
public:
  using ReplyFn = dust::Function<void(const std::string& content)>;

  using Agent::Agent;
  ~ProfessionalAgent() override;

  void Input(const std::string& input, ReplyFn reply);

private:
  // prototype: no implementation details yet
};

} // namespace agent
