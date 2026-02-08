#pragma once

#include "agent/history.h"
#include "agent/session.h"
#include "agent/system_prompt_segment.h"
#include "tool/tool.h"

#include "dust/memory/ref_counted.h"
#include "dust/memory/ref_ptr.h"

#include <vector>

namespace agent {

class AgentContext final : public dust::RefCounted {
 public:
  class Builder;

  AgentContext(const AgentContext&) = delete;
  AgentContext& operator=(const AgentContext&) = delete;

  const dust::RefPtr<Session>& session() const;
  const std::vector<dust::RefPtr<SystemPromptSegment>>& system_segments() const;
  const dust::RefPtr<History>& history() const;
  const std::vector<dust::RefPtr<Tool>>& tools() const;

 private:
  friend class Builder;

  AgentContext() = delete;

  AgentContext(dust::RefPtr<Session> session,
               std::vector<dust::RefPtr<SystemPromptSegment>> system_segments,
               dust::RefPtr<History> history,
               std::vector<dust::RefPtr<Tool>> tools)
      : session_(std::move(session)),
        system_segments_(std::move(system_segments)),
        history_(std::move(history)),
        tools_(std::move(tools)) {}

  dust::RefPtr<Session> session_;
  std::vector<dust::RefPtr<SystemPromptSegment>> system_segments_;
  dust::RefPtr<History> history_;
  std::vector<dust::RefPtr<Tool>> tools_;
};

class AgentContext::Builder {
 public:
  Builder() = default;

  Builder(const Builder&) = delete;
  Builder& operator=(const Builder&) = delete;

  Builder& SetSession(dust::RefPtr<Session> session);
  Builder& AddSystemSegment(dust::RefPtr<SystemPromptSegment> segment);
  Builder& SetHistory(dust::RefPtr<History> history);
  Builder& AddTool(dust::RefPtr<Tool> tool);

  dust::RefPtr<AgentContext> Build() &&;

 private:
  dust::RefPtr<Session> session_;
  std::vector<dust::RefPtr<SystemPromptSegment>> system_segments_;
  dust::RefPtr<History> history_;
  std::vector<dust::RefPtr<Tool>> tools_;
};

}  // namespace agent
