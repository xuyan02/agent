#pragma once

#include "core/tool_protocol.h"

#include <vector>

namespace agent {

class Conversation final {
public:
  void add(Message msg) { messages_.push_back(std::move(msg)); }
  [[nodiscard]] const std::vector<Message>& messages() const { return messages_; }

  // Best-effort: return pointer to the first system message (if any).
  [[nodiscard]] Message* first_system_message();

private:
  std::vector<Message> messages_;
};

} // namespace agent
