#include "agent/agent_context.h"

#include "agent/in_memory_history.h"

#include "dust/memory/ref_ptr.h"

#include <utility>

namespace agent {

const dust::RefPtr<Session>& AgentContext::session() const {
  return session_;
}

const std::vector<dust::RefPtr<SystemPromptSegment>>& AgentContext::system_segments() const {
  return system_segments_;
}

const dust::RefPtr<History>& AgentContext::history() const {
  return history_;
}

const std::vector<dust::RefPtr<Tool>>& AgentContext::tools() const {
  return tools_;
}

AgentContext::Builder& AgentContext::Builder::SetSession(dust::RefPtr<Session> session) {
  session_ = std::move(session);
  return *this;
}

AgentContext::Builder& AgentContext::Builder::AddSystemSegment(
    dust::RefPtr<SystemPromptSegment> segment) {
  if (segment)
    system_segments_.push_back(std::move(segment));
  return *this;
}

AgentContext::Builder& AgentContext::Builder::SetHistory(dust::RefPtr<History> history) {
  history_ = std::move(history);
  return *this;
}

AgentContext::Builder& AgentContext::Builder::AddTool(dust::RefPtr<Tool> tool) {
  if (tool)
    tools_.push_back(std::move(tool));
  return *this;
}

dust::RefPtr<AgentContext> AgentContext::Builder::Build() && {
  dust::RefPtr<Session> session = std::move(session_);
  if (!session)
    session = std::move(Session::Builder()).Build();

  dust::RefPtr<History> history = std::move(history_);
  if (!history)
    history = dust::MakeRefPtr<InMemoryHistory>();

  return dust::RefPtr<AgentContext>::Adopt(new AgentContext(
      std::move(session), std::move(system_segments_), std::move(history), std::move(tools_)));
}

}  // namespace agent
