#include "agent/agent_context.h"
#include "agent/in_memory_history.h"
#include "agent/session.h"
#include "agent/tool_call_executor.h"
#include "llm/chat_message.h"
#include "llm/llm_provider.h"
#include "llm/llm_sender.h"
#include "tool/tool.h"
#include "tool/tool_spec.h"

#include "dust/async/just.h"
#include "dust/memory/ref_ptr.h"

#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

class ReadyJsonFuture final : public dust::Future<nlohmann::json> {
 public:
  explicit ReadyJsonFuture(nlohmann::json v) : v_(std::move(v)) {}

  dust::Poll<nlohmann::json> PollOnce(dust::PollContext&) override {
    if (done_)
      return dust::Poll<nlohmann::json>::Ready(nlohmann::json{{"error", "polled twice"}});
    done_ = true;
    return dust::Poll<nlohmann::json>::Ready(std::move(v_));
  }

 private:
  nlohmann::json v_;
  bool done_ = false;
};

class FakeTool final : public agent::Tool {
 public:
  FakeTool(std::string function_name, nlohmann::json result)
      : function_name_(std::move(function_name)), result_(std::move(result)) {
    agent::FunctionSpec::Builder fb;
    fb.SetName(function_name_).SetDescription("fake");

    agent::ToolSpec::Builder tb;
    tb.SetName("fake").SetDescription("fake").AddFunction(std::move(fb).Build());

    spec_ = std::make_unique<agent::ToolSpec>(std::move(tb).Build());
  }

  const agent::ToolSpec* GetSpec() const override { return spec_.get(); }

  dust::FuturePtr<nlohmann::json> Invoke(dust::RefPtr<agent::AgentContext>,
                                        const std::string& function_name,
                                        const nlohmann::json&) override {
    // Tool::Invoke is infallible. nullptr means function not found.
    if (function_name != function_name_)
      return nullptr;
    return dust::MakeRefPtr<ReadyJsonFuture>(result_);
  }

 private:
  std::string function_name_;
  nlohmann::json result_;
  std::unique_ptr<agent::ToolSpec> spec_;
};

class FakeSender final : public agent::LlmSender {
 public:
  FakeSender(std::string model, int* phase) : agent::LlmSender(std::move(model)), phase_(phase) {}

  dust::FuturePtr<dust::Result<dust::RefPtr<agent::ChatMessage>, std::string>> Send(
      std::vector<dust::RefPtr<agent::ChatMessage>> messages,
      std::vector<agent::Tool*>) override {
    seen_messages_.push_back(std::move(messages));

    // First round: request tool call.
    if (phase_ && *phase_ == 0) {
      ++(*phase_);

      nlohmann::json tool_calls = nlohmann::json::array();
      tool_calls.push_back({
          {"id", "call_1"},
          {"type", "function"},
          {"function", {{"name", "fake.fn"}, {"arguments", "{}"}}},
      });

      auto msg = agent::ChatMessage::CreateToolCalls(agent::ChatRole::kAssistant, tool_calls);
      return dust::Just(dust::Result<dust::RefPtr<agent::ChatMessage>, std::string>::Ok(msg));
    }

    // Second round: should include a tool_result.
    bool saw_tool_result = false;
    for (const auto& m : seen_messages_.back()) {
      if (m && m->role() == agent::ChatRole::kTool && m->content() &&
          m->content()->kind() == agent::ChatContent::Kind::kToolResult) {
        const auto* tr = static_cast<const agent::ChatContent::ToolResult*>(m->content());
        if (tr->tool_call_id() == "call_1")
          saw_tool_result = true;
      }
    }
    assert(saw_tool_result);

    if (phase_)
      ++(*phase_);
    auto msg = agent::ChatMessage::CreateText(agent::ChatRole::kAssistant, "done");
    return dust::Just(dust::Result<dust::RefPtr<agent::ChatMessage>, std::string>::Ok(msg));
  }

 private:
  int* phase_ = nullptr;
  std::vector<std::vector<dust::RefPtr<agent::ChatMessage>>> seen_messages_;
};

class FakeProvider final : public agent::LlmProvider {
 public:
  std::unique_ptr<agent::LlmSender> CreateSender(std::string model_name) override {
    return std::make_unique<FakeSender>(std::move(model_name), &phase_);
  }

 private:
  int phase_ = 0;
};

}  // namespace

int main() {
  dust::RefPtr<agent::InMemoryHistory> hist = dust::MakeRefPtr<agent::InMemoryHistory>();

  agent::Session::Builder sb;
  sb.SetDefaultModel("m");
  sb.AddLlmProvider(std::make_unique<FakeProvider>());
  dust::RefPtr<agent::Session> session = std::move(sb).Build();

  agent::AgentContext::Builder cb;
  cb.SetSession(session);
  cb.SetHistory(hist);
  cb.AddTool(dust::MakeRefPtr<FakeTool>("fake.fn", nlohmann::json{{"ok", true}}));
  dust::RefPtr<agent::AgentContext> ctx = std::move(cb).Build();

  // Seed a user message.
  {
    auto f = hist->Append(ctx, agent::ChatMessage::CreateText(agent::ChatRole::kUser, "hi"));
    assert(f);
    dust::PollContext poll_ctx{dust::WakerHandle()};
    auto polled = f->PollOnce(poll_ctx);
    assert(polled.is_ready());
    assert(polled.TakeReady().ok());
  }

  agent::ToolCallExecutor* exec = session->tool_call_executor();
  assert(exec);

  auto f = exec->Send(ctx);
  assert(f);

  dust::PollContext poll_ctx{dust::WakerHandle()};
  dust::Poll<dust::Result<dust::RefPtr<agent::ChatMessage>, std::string>> polled =
      dust::Poll<dust::Result<dust::RefPtr<agent::ChatMessage>, std::string>>::Pending();

  // Drive manually (no executor/waker in this unit test).
  for (int i = 0; i < 1000 && polled.is_pending(); ++i)
    polled = f->PollOnce(poll_ctx);

  assert(polled.is_ready());

  auto r = polled.TakeReady();
  assert(r.ok());
  assert(r.value());
  assert(r.value()->content());
  assert(r.value()->content()->kind() == agent::ChatContent::Kind::kText);

  // History should end with assistant text.
  dust::RefPtr<agent::ChatMessage> last = hist->GetLast(ctx);
  assert(last);
  assert(last->role() == agent::ChatRole::kAssistant);
  assert(last->content());
  assert(last->content()->kind() == agent::ChatContent::Kind::kText);

  return 0;
}
