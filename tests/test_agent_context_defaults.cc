#include "agent/agent_context.h"

#include "agent/in_memory_history.h"

#include "dust/memory/ref_ptr.h"

#include <cassert>

int main() {
  dust::RefPtr<agent::AgentContext> ctx = std::move(agent::AgentContext::Builder()).Build();
  assert(ctx);

  // Defaults.
  assert(ctx->session());
  assert(ctx->history());
  assert(dynamic_cast<agent::InMemoryHistory*>(ctx->history().Get()) != nullptr);
  assert(ctx->system_segments().empty());
  assert(ctx->tools().empty());

  return 0;
}
