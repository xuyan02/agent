#pragma once

#include "agent/agent_context.h"

#include "dust/async/future.h"
#include "dust/memory/ref_ptr.h"

namespace agent {

class Agent {
 public:
  virtual ~Agent() = default;

  Agent(const Agent&) = delete;
  Agent& operator=(const Agent&) = delete;

  virtual dust::FuturePtr<dust::Result<void, std::string>> Run(
      dust::RefPtr<AgentContext> context) = 0;

 protected:
  Agent() = default;
};

}  // namespace agent
