#include "agent/llm_agent.h"

#include "agent/agent_context.h"
#include "agent/session.h"
#include "agent/tool_call_executor.h"

#include "dust/async/just.h"
#include "dust/memory/ref_ptr.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace agent {

namespace {

bool DebugAgent() {
  const char* v = std::getenv("CPP_AGENT_DEBUG_AGENT");
  return v && v[0] != 0;
}

using SendResult = dust::Result<void, std::string>;

}  // namespace

LlmAgent::LlmAgent() = default;

LlmAgent::~LlmAgent() = default;

dust::FuturePtr<dust::Result<void, std::string>> LlmAgent::Run(
    dust::RefPtr<AgentContext> context) {
  // Pseudo-code ("C++ + await"):
  // {
  //   std::string model = context->session()->default_model();
  //   if (model.empty())
  //     return;
  //
  //   std::unique_ptr<LlmSender> sender = context->session()->CreateSender(model);
  //   if (!sender)
  //     return;
  //
  //   RefPtr<History> history = context->history();
  //   auto messages = await#1 history->GetAll(context);
  //
  //   std::vector<Tool*> tools;
  //   auto reply_or_err = await#2 sender->Send(std::move(messages), std::move(tools));
  //   if (!reply_or_err.ok())
  //     return;
  //
  //   RefPtr<ChatMessage> reply = std::move(reply_or_err.value());
  //   if (!reply)
  //     return;
  //
  //   // ToolCallExecutor appends assistant messages (including tool calls, tool results, and final text).
  //   return;
  // }

  class SendOnceFuture final : public dust::Future<dust::Result<void, std::string>> {
   public:
    // Pseudo-code ("C++ + await"):
    // {
    //   std::string model = context->session()->default_model();
    //   if (model.empty())
    //     return;
    //
    //   std::unique_ptr<LlmSender> sender = context->session()->CreateSender(model);
    //   if (!sender)
    //     return;
    //
    //   RefPtr<History> history = context->history();
    //   auto messages = await#1 history->GetAll(context);
    //
    //   std::vector<Tool*> tools;
    //   auto reply_or_err = await#2 sender->Send(std::move(messages), std::move(tools));
    //   if (!reply_or_err.ok())
    //     return;
    //
    //   RefPtr<ChatMessage> reply = std::move(reply_or_err.value());
    //   if (!reply)
    //     return;
    //
    //   // ToolCallExecutor appends assistant messages (including tool calls, tool results, and final text).
    //   return;
    // }
   public:
    explicit SendOnceFuture(dust::RefPtr<AgentContext> context) : context_(std::move(context)) {}

    dust::Poll<dust::Result<void, std::string>> PollOnce(dust::PollContext& ctx) override {
      if (DebugAgent())
        std::fprintf(stderr, "[cpp-agent.agent] LlmAgent::Run PollOnce state=%d\n", static_cast<int>(state_));

      dust::PollContext child_ctx(ctx.waker());

      while (state_ != State::kDone) {
        switch (state_) {
          case State::kInit: {
            if (!context_ || !context_->session() || !context_->history()) {
              state_ = State::kDone;
              done_ = dust::Result<void, std::string>::Err("agent: missing context/session/history");
              break;
            }

            std::string model = context_->session()->default_model();
            if (model.empty()) {
              state_ = State::kDone;
              done_ = dust::Result<void, std::string>::Err("agent: missing default model");
              break;
            }

            sender_ = context_->session()->CreateSender(model);
            if (!sender_) {
              state_ = State::kDone;
              done_ = dust::Result<void, std::string>::Err("agent: failed to create sender");
              break;
            }

            history_ = context_->history();
            get_all_ = history_->GetAll(context_);
            if (!get_all_) {
              state_ = State::kDone;
              done_ = dust::Result<void, std::string>::Err("agent: history GetAll returned null future");
              break;
            }

            state_ = State::kAwait1;
            break;
          }

          case State::kAwait1: {
            if (DebugAgent())
              std::fprintf(stderr, "[cpp-agent.agent] await#1: history->GetAll\n");

            auto polled = get_all_->PollOnce(child_ctx);
            if (polled.is_pending())
              return dust::Poll<dust::Result<void, std::string>>::Pending();

            messages_ = polled.TakeReady();
            get_all_ = nullptr;

            send_ = context_->session()->tool_call_executor()->Send(context_);
            if (!send_) {
              state_ = State::kDone;
              done_ = dust::Result<void, std::string>::Err("agent: tool loop returned null future");
              break;
            }

            state_ = State::kAwait2;
            break;
          }

          case State::kAwait2: {
            if (DebugAgent())
              std::fprintf(stderr, "[cpp-agent.agent] await#2: tool_loop->Send\n");

            auto polled = send_->PollOnce(child_ctx);
            if (polled.is_pending())
              return dust::Poll<dust::Result<void, std::string>>::Pending();

            auto r = polled.TakeReady();
            send_ = nullptr;
            if (!r.ok()) {
              state_ = State::kDone;
              done_ = dust::Result<void, std::string>::Err(r.error());
              break;
            }

            auto reply = std::move(r.value());
            if (!reply) {
              state_ = State::kDone;
              done_ = dust::Result<void, std::string>::Err("agent: sender returned null reply");
              break;
            }

            // ToolCallExecutor already appended the final assistant message.
            (void)reply;

            done_ = dust::Result<void, std::string>::Ok();
            state_ = State::kDone;
            break;
          }


          case State::kDone:
            break;
        }
      }

      return dust::Poll<dust::Result<void, std::string>>::Ready(std::move(done_));
    }

   private:
    enum class State { kInit, kAwait1, kAwait2, kDone };

    dust::RefPtr<AgentContext> context_;

    State state_ = State::kInit;

    std::unique_ptr<LlmSender> sender_;
    dust::RefPtr<History> history_;

    dust::FuturePtr<std::vector<dust::RefPtr<ChatMessage>>> get_all_;
    std::vector<dust::RefPtr<ChatMessage>> messages_;

    dust::FuturePtr<dust::Result<dust::RefPtr<ChatMessage>, std::string>> send_;
    dust::Result<void, std::string> done_ = dust::Result<void, std::string>::Ok();

  };

  return dust::MakeRefPtr<SendOnceFuture>(std::move(context));
}

}  // namespace agent
