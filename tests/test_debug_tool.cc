#include "tool/debug_tool.h"

#include "agent/agent_context.h"
#include "agent/session.h"

#include "dust/memory/ref_ptr.h"

#include <cassert>

#include <nlohmann/json.hpp>

int main() {
  {
    auto tool = dust::MakeRefPtr<agent::DebugTool>();
    assert(tool);

    auto session = std::move(agent::Session::Builder().SetDefaultModel("fake")).Build();
    auto ctx = std::move(agent::AgentContext::Builder().SetSession(session)).Build();

    nlohmann::json args;
    args["text"] = "hello";

    auto fut = tool->Invoke(ctx, "debug.echo", args);
    assert(fut);

    dust::PollContext pc{dust::WakerHandle()};
    auto polled = fut->PollOnce(pc);
    assert(polled.is_ready());

    nlohmann::json out = polled.TakeReady();
    assert(out.is_string());
    assert(out == "hello");
  }


  return 0;
}
