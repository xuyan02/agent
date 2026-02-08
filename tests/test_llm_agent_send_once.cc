#include "agent/agent_context.h"
#include "agent/in_memory_history.h"
#include "agent/llm_agent.h"
#include "agent/session.h"
#include "llm/chat_message.h"
#include "llm/llm_provider.h"
#include "llm/llm_sender.h"

#include "dust/memory/ref_ptr.h"

#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

class FakeSender final : public agent::LlmSender {
 public:
  FakeSender(std::string model, dust::RefPtr<agent::ChatMessage> reply)
      : agent::LlmSender(std::move(model)), reply_(std::move(reply)) {}

  dust::FuturePtr<dust::Result<dust::RefPtr<agent::ChatMessage>, std::string>> Send(
      std::vector<dust::RefPtr<agent::ChatMessage>> messages,
      std::vector<agent::Tool*>) override {
    seen_messages_ = std::move(messages);

    class ReplyFuture final
        : public dust::Future<dust::Result<dust::RefPtr<agent::ChatMessage>, std::string>> {
     public:
      explicit ReplyFuture(dust::RefPtr<agent::ChatMessage> reply) : reply_(std::move(reply)) {}

      dust::Poll<dust::Result<dust::RefPtr<agent::ChatMessage>, std::string>> PollOnce(
          dust::PollContext&) override {
        if (done_)
          return dust::Poll<dust::Result<dust::RefPtr<agent::ChatMessage>, std::string>>::Ready(
              dust::Result<dust::RefPtr<agent::ChatMessage>, std::string>::Err("polled twice"));

        done_ = true;
        return dust::Poll<dust::Result<dust::RefPtr<agent::ChatMessage>, std::string>>::Ready(
            dust::Result<dust::RefPtr<agent::ChatMessage>, std::string>::Ok(std::move(reply_)));
      }

     private:
      dust::RefPtr<agent::ChatMessage> reply_;
      bool done_ = false;
    };

    return dust::MakeRefPtr<ReplyFuture>(reply_);
  }

  const std::vector<dust::RefPtr<agent::ChatMessage>>& seen_messages() const {
    return seen_messages_;
  }

 private:
  dust::RefPtr<agent::ChatMessage> reply_;
  std::vector<dust::RefPtr<agent::ChatMessage>> seen_messages_;
};

class FakeProvider final : public agent::LlmProvider {
 public:
  explicit FakeProvider(dust::RefPtr<agent::ChatMessage> reply) : reply_(std::move(reply)) {}

  std::unique_ptr<agent::LlmSender> CreateSender(std::string model_name) override {
    last_model_ = model_name;
    return std::make_unique<FakeSender>(std::move(model_name), reply_);
  }

  const std::string& last_model() const { return last_model_; }

 private:
  dust::RefPtr<agent::ChatMessage> reply_;
  std::string last_model_;
};

}  // namespace

int main() {
  auto user = agent::ChatMessage::CreateText(agent::ChatRole::kUser, "hello");
  auto assistant = agent::ChatMessage::CreateText(agent::ChatRole::kAssistant, "world");

  dust::RefPtr<agent::InMemoryHistory> hist = dust::MakeRefPtr<agent::InMemoryHistory>();

  agent::Session::Builder sb;
  sb.SetDefaultModel("m");
  auto provider = std::make_unique<FakeProvider>(assistant);
  FakeProvider* provider_raw = provider.get();
  sb.AddLlmProvider(std::move(provider));
  dust::RefPtr<agent::Session> session = std::move(sb).Build();

  agent::AgentContext::Builder cb;
  cb.SetSession(session);
  cb.SetHistory(hist);
  dust::RefPtr<agent::AgentContext> ctx = std::move(cb).Build();

  {
    auto f = hist->Append(ctx, user);
    assert(f);

    dust::PollContext poll_ctx{dust::WakerHandle()};
    auto polled = f->PollOnce(poll_ctx);
    assert(polled.is_ready());
  }

  agent::LlmAgent agent;

  {
    auto f = agent.Run(ctx);
    assert(f);

    dust::PollContext poll_ctx{dust::WakerHandle()};
    auto polled = f->PollOnce(poll_ctx);
    assert(polled.is_ready());
  }

  // Provider was used with default model.
  assert(provider_raw->last_model() == "m");

  // Assistant reply appended.
  assert(hist->GetLast(ctx).Get() == assistant.Get());

  return 0;
}
