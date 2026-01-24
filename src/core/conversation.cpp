#include "core/conversation.h"

namespace cpp_agent::core {

Message* Conversation::first_system_message() {
  for (auto& m : messages_) {
    if (m.role == Role::kSystem) return &m;
  }
  return nullptr;
}

} // namespace cpp_agent::core
