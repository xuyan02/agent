#include "agent/agent_runner.h"

#include "agent/agent_context.h"

#include "dust/memory/ref_ptr.h"
#include "dust/message_loop/message_loop.h"
#include "llm/chat_message.h"

#include <utility>

namespace agent {

namespace {

}  // namespace

AgentRunner::AgentRunner(std::unique_ptr<Agent> agent) : agent_(std::move(agent)) {}

AgentRunner::~AgentRunner() = default;

void AgentRunner::Run(dust::RefPtr<Session> session, std::unique_ptr<Console> console) {
  session_ = std::move(session);
  console_ = std::move(console);

  AgentContext::Builder builder;
  builder.SetSession(session_);

  if (session_) {
    builder.SetHistory(session_->history());

    for (const auto& seg : session_->system_segments()) {
      builder.AddSystemSegment(seg);
    }

    for (const auto& tool : session_->tools()) {
      builder.AddTool(tool);
    }
  }

  dust::RefPtr<AgentContext> ctx = std::move(builder).Build();

  if (!console_ || !agent_ || !ctx || !ctx->history())
    return;

  console_->SetOnLine([this, ctx = std::move(ctx)](std::string line) mutable {
    const char* dbg = std::getenv("CPP_AGENT_DEBUG_AGENT_RUNNER");
    const bool debug = dbg && dbg[0] != 0;
    if (debug)
      std::fprintf(stderr, "[cpp-agent.runner] on_line len=%zu\n", line.size());
    if (!ctx || !ctx->history() || !ctx->session())
      return;

    auto user = ChatMessage::CreateText(ChatRole::kUser, std::move(line));

    // Pseudo-code ("C++ + await"):
    // {
    //   Result<void, string> r1 = await#1 ctx->history()->Append(ctx, user);
    //   if (!r1.ok())
    //     return Err(r1.error());
    //
    //   Result<void, string> r2 = await#2 runner->agent()->Run(ctx);
    //   if (!r2.ok())
    //     return Err(r2.error());
    //
    //   return Ok();
    // }

    class RunLineFuture final : public dust::Future<dust::Result<void, std::string>> {
     public:
      RunLineFuture(AgentRunner* runner,
                    dust::RefPtr<AgentContext> ctx,
                    dust::RefPtr<ChatMessage> user)
          : runner_(runner), ctx_(std::move(ctx)), user_(std::move(user)) {}

      dust::Poll<dust::Result<void, std::string>> PollOnce(dust::PollContext& ctx) override {
        const char* dbg = std::getenv("CPP_AGENT_DEBUG_AGENT_RUNNER");
        const bool debug = dbg && dbg[0] != 0;
        dust::PollContext child_ctx(ctx.waker());

        while (state_ != State::kDone) {
          if (debug)
            std::fprintf(stderr,
                         "[cpp-agent.runner] PollOnce loop state=%d\n",
                         static_cast<int>(state_));
          switch (state_) {
            case State::kInit: {
              if (!runner_ || !runner_->agent() || !ctx_ || !ctx_->history()) {
                done_ = dust::Result<void, std::string>::Err("runner: internal error");
                state_ = State::kDone;
                break;
              }

              append_ = ctx_->history()->Append(ctx_, std::move(user_));
              if (!append_) {
                done_ = dust::Result<void, std::string>::Err("runner: history Append returned null future");
                state_ = State::kDone;
                break;
              }

              state_ = State::kAwait1;
              break;
            }

            case State::kAwait1: {
              auto polled = append_->PollOnce(child_ctx);
              if (polled.is_pending())
                return dust::Poll<dust::Result<void, std::string>>::Pending();

              auto r = polled.TakeReady();
              append_ = nullptr;
              if (!r.ok()) {
                done_ = dust::Result<void, std::string>::Err(r.error());
                state_ = State::kDone;
                break;
              }

              run_ = runner_->agent()->Run(ctx_);
              if (!run_) {
                done_ = dust::Result<void, std::string>::Err("runner: agent Run returned null future");
                state_ = State::kDone;
                break;
              }

              state_ = State::kAwait2;
              break;
            }

            case State::kAwait2: {
              auto polled = run_->PollOnce(child_ctx);
              if (polled.is_pending())
                return dust::Poll<dust::Result<void, std::string>>::Pending();

              if (debug)
                std::fprintf(stderr, "[cpp-agent.runner] kAwait2 -> Ready\n");

              auto r = polled.TakeReady();
              run_ = nullptr;
              if (!r.ok()) {
                done_ = dust::Result<void, std::string>::Err(r.error());
                state_ = State::kDone;
                break;
              }

              if (debug && ctx_ && ctx_->history()) {
                auto last = ctx_->history()->GetLast(ctx_);
                if (!last) {
                  std::fprintf(stderr, "[cpp-agent.runner] history last: (null)\n");
                } else {
                  std::fprintf(stderr,
                               "[cpp-agent.runner] history last: role=%d kind=%d\n",
                               static_cast<int>(last->role()),
                               last->content() ? static_cast<int>(last->content()->kind()) : 0);
                }
              }

              // Interactive echo: print the latest assistant text, even if the most recent
              // history entry is a tool call / tool result.
              if (runner_ && runner_->console_ && ctx_ && ctx_->history()) {
                auto all = ctx_->history()->GetAll(ctx_);
                if (all) {
                  auto polled_all = all->PollOnce(child_ctx);
                  if (polled_all.is_ready()) {
                    auto msgs = polled_all.TakeReady();
                    for (auto it = msgs.rbegin(); it != msgs.rend(); ++it) {
                      const auto& m = *it;
                      if (!m || m->role() != ChatRole::kAssistant)
                        continue;
                      if (!m->content() || m->content()->kind() != ChatContent::Kind::kText)
                        continue;
                      auto* text = static_cast<const ChatContent::Text*>(m->content());
                      runner_->console_->PrintLine(text->text());
                      break;
                    }
                  }
                }
              }

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

      AgentRunner* runner_ = nullptr;
      dust::RefPtr<AgentContext> ctx_;
      dust::RefPtr<ChatMessage> user_;

      State state_ = State::kInit;
      dust::Result<void, std::string> done_ = dust::Result<void, std::string>::Ok();

      dust::FuturePtr<dust::Result<void, std::string>> append_;
      dust::FuturePtr<dust::Result<void, std::string>> run_;
    };

    auto f = dust::MakeRefPtr<RunLineFuture>(this, ctx, std::move(user));
    if (auto* loop = dust::MessageLoop::Current()) {
      loop->executor()->Spawn<dust::Result<void, std::string>>(
          std::move(f),
          dust::OnceFunction<void(dust::Result<void, std::string>)>(
              [](dust::Result<void, std::string> r) {
                if (!r.ok())
                  std::fprintf(stderr, "error: %s\n", r.error().c_str());
              }));
    }
  });
}

}  // namespace agent
