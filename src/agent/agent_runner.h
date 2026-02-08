#pragma once

#include "agent/agent.h"
#include "agent/session.h"
#include "console/console.h"

#include "dust/memory/ref_ptr.h"

#include <memory>

namespace agent {

class AgentRunner {
 public:
  explicit AgentRunner(std::unique_ptr<Agent> agent);
  ~AgentRunner();

  AgentRunner(const AgentRunner&) = delete;
  AgentRunner& operator=(const AgentRunner&) = delete;

  void Run(dust::RefPtr<Session> session, std::unique_ptr<Console> console);
  Console* console() const { return console_.get(); }
  Agent* agent() const { return agent_.get(); }

 private:
  std::unique_ptr<Agent> agent_;
  std::unique_ptr<Console> console_;
  dust::RefPtr<Session> session_;
};

}  // namespace agent
