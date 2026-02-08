#pragma once

#include "agent/history.h"

namespace agent {

class InMemoryHistory final : public History {
 public:
  InMemoryHistory();
  ~InMemoryHistory() override;

  InMemoryHistory(const InMemoryHistory&) = delete;
  InMemoryHistory& operator=(const InMemoryHistory&) = delete;

  dust::FuturePtr<std::vector<dust::RefPtr<ChatMessage>>> GetAll(
      dust::RefPtr<AgentContext> context) override;

  dust::FuturePtr<dust::Result<void, std::string>> Append(
      dust::RefPtr<AgentContext> context,
      dust::RefPtr<ChatMessage> message) override;

  dust::RefPtr<ChatMessage> GetLast(dust::RefPtr<AgentContext> context) override;

 private:
  std::vector<dust::RefPtr<ChatMessage>> messages_;
};

}  // namespace agent
