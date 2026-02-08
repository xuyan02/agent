#include "agent/agent_context.h"
#include "agent/in_memory_history.h"
#include "agent/session.h"
#include "llm/chat_message.h"

#include "dust/memory/ref_ptr.h"

#include <cassert>
#include <string>
#include <utility>
#include <vector>

int main() {
  dust::RefPtr<agent::InMemoryHistory> h = dust::MakeRefPtr<agent::InMemoryHistory>();

  agent::AgentContext::Builder b;
  b.SetSession(std::move(agent::Session::Builder()).Build());
  b.SetHistory(h);
  dust::RefPtr<agent::AgentContext> ctx = std::move(b).Build();

  assert(!h->GetLast(ctx));

  auto m1 = agent::ChatMessage::CreateText(agent::ChatRole::kUser, "hi");
  {
    auto f = h->Append(ctx, m1);
    assert(f);

    dust::PollContext poll_ctx{dust::WakerHandle()};
    auto polled = f->PollOnce(poll_ctx);
    assert(polled.is_ready());
  }
  assert(h->GetLast(ctx).Get() == m1.Get());

  std::vector<dust::RefPtr<agent::ChatMessage>> all;
  {
    auto f = h->GetAll(ctx);
    assert(f);

    dust::PollContext poll_ctx{dust::WakerHandle()};
    auto polled = f->PollOnce(poll_ctx);
    assert(polled.is_ready());

    all = polled.TakeReady();
  }
  assert(all.size() == 1);
  assert(all[0].Get() == m1.Get());

  return 0;
}
