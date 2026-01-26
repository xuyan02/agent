#include "runtime/professional_agent.h"

namespace agent {

ProfessionalAgent::~ProfessionalAgent() = default;

void ProfessionalAgent::Input(const std::string& input, ReplyFn reply) {
  // Prototype behavior: echo.
  if (reply) reply(input);
}

} // namespace agent
