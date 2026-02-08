#include "console/cli_console.h"

#include "agent/agent_runner.h"
#include "agent/llm_agent.h"
#include "agent/session.h"
#include "config/agent_config.h"
#include "console/cli_console.h"
#include "llm/openai/openai_provider.h"
#include "tool/debug_tool.h"
#include "tool/file_tool.h"

#include "dust/message_loop/linux_message_pump_epoll.h"
#include "dust/message_loop/message_loop.h"

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include <sys/epoll.h>
#include <unistd.h>

namespace {

bool DebugAgentCli() {
  const char* v = std::getenv("CPP_AGENT_DEBUG_AGENT_CLI");
  return v && v[0] != 0;
}

constexpr const char* kUsage =
    "agent_cli [--input <text>]\n"
    "\n"
    "Runs an agent.\n"
    "- Loads config from <cwd>/.agent/agent.yaml\n"
    "\n"
    "Modes:\n"
    "  (default) interactive console\n"
    "  --input <text> single-shot mode (no stdin watch)\n";

}  // namespace

int main(int argc, char** argv) {
  std::string input;

  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      std::cout << kUsage;
      return 0;
    }
    if (arg == "--input") {
      if (i + 1 >= argc) {
        std::cerr << "error: --input requires a value\n";
        return 2;
      }
      input = argv[++i];
      continue;
    }

    std::cerr << "error: unknown arg: " << arg << "\n";
    std::cerr << kUsage;
    return 2;
  }

  const std::filesystem::path workspace = std::filesystem::current_path();
  const std::filesystem::path agent_dir = workspace / ".agent";
  const std::filesystem::path cfg_path = agent_dir / "agent.yaml";

  std::string cfg_err;
  std::optional<agent::AgentConfig> cfg = agent::LoadAgentConfigYaml(cfg_path.string(), &cfg_err);
  if (!cfg) {
    std::cerr << "error: failed to load config: " << cfg_err << "\n";
    std::cerr << "hint: create " << cfg_path << "\n";
    return 1;
  }

  agent::Session::Builder session_builder;
  session_builder.SetWorkspacePath(workspace);
  session_builder.SetAgentPath(agent_dir);
  session_builder.SetDefaultModel(cfg->model);
  session_builder.AddTool(dust::MakeRefPtr<agent::DebugTool>());
  session_builder.AddTool(dust::MakeRefPtr<agent::FileTool>());

  if (cfg->openai) {
    session_builder.AddLlmProvider(std::make_unique<agent::OpenAiProvider>(
        cfg->openai->base_url, cfg->openai->api_key));
  }

  dust::RefPtr<agent::Session> session = std::move(session_builder).Build();

  auto pump = std::make_unique<dust::LinuxMessagePumpEpoll>();
  dust::MessageLoop loop(std::move(pump));

  agent::AgentRunner runner(std::make_unique<agent::LlmAgent>());

  if (!input.empty()) {
    // Single-shot mode: no stdin watch.
    // Preflight: ensure sender can be created AND that immediate failures are surfaced without
    // entering loop.Run(). Note: low-level IO futures require MessageLoop::Current().
    {
      const std::string& model = session->default_model();
      if (model.empty()) {
        std::cerr << "error: missing default model\n";
        return 1;
      }
      std::unique_ptr<agent::LlmSender> sender = session->CreateSender(model);
      if (!sender) {
        std::cerr << "error: failed to create LLM sender (check provider config/api key)\n";
        return 1;
      }

      // Also preflight the first request path: enqueue a minimal RunOnceFuture and ensure it is
      // polled at least once (so synchronous errors become Ready immediately).
    }

    agent::AgentContext::Builder b;
    b.SetSession(session);
    b.SetHistory(session->history());
    for (const auto& seg : session->system_segments())
      b.AddSystemSegment(seg);
    for (const auto& tool : session->tools())
      b.AddTool(tool);

    dust::RefPtr<agent::AgentContext> ctx = std::move(b).Build();

    // Pseudo-code ("C++ + await"):
    // {
    //   auto user = ChatMessage::CreateText(ChatRole::kUser, input);
    //   await#1 ctx->history()->Append(ctx, user);
    //
    //   Agent* a = runner.agent();
    //   if (!a)
    //     return Err("missing agent");
    //
    //   await#2 a->Run(ctx);
    //
    //   RefPtr<ChatMessage> last = ctx->history()->GetLast(ctx);
    //   if (!last || last->role() != ChatRole::kAssistant || !last->content())
    //     return Err("no assistant reply");
    //   if (last->content()->kind() != ChatContent::Kind::kText)
    //     return Err("assistant reply is not text");
    //
    //   auto* text = static_cast<const ChatContent::Text*>(last->content());
    //   std::cout << text->text() << "\n";
    //   return Ok();
    // }

    class RunOnceFuture final : public dust::Future<void> {
     public:
      RunOnceFuture(agent::AgentRunner* runner,
                   dust::RefPtr<agent::AgentContext> ctx,
                   dust::MessageLoop* loop,
                   std::string input)
          : runner_(runner), ctx_(std::move(ctx)), loop_(loop), input_(std::move(input)) {}

      dust::Poll<void> PollOnce(dust::PollContext& ctx) override {
        if (DebugAgentCli()) {
          std::fprintf(stderr,
                       "[cpp-agent.cli] PollOnce state=%d append=%d run=%d\n",
                       static_cast<int>(state_), append_ ? 1 : 0, run_ ? 1 : 0);
        }

        while (state_ != State::kDone) {
          switch (state_) {
            case State::kInit: {
              if (DebugAgentCli())
                std::fprintf(stderr, "[cpp-agent.cli]  kInit enter\n");

              if (!ctx_ || !ctx_->history() || !runner_ || !loop_) {
                Fail("internal error");
                return dust::Poll<void>::Ready();
              }

              auto user = agent::ChatMessage::CreateText(agent::ChatRole::kUser, std::move(input_));
              append_ = ctx_->history()->Append(ctx_, std::move(user));
              if (DebugAgentCli())
                std::fprintf(stderr, "[cpp-agent.cli]  kInit after Append: append=%d\n", append_ ? 1 : 0);

              if (!append_) {
                Fail("internal error");
                return dust::Poll<void>::Ready();
              }

              state_ = State::kAwait1;
              break;
            }

            case State::kAwait1: {
              if (DebugAgentCli())
                std::fprintf(stderr, "[cpp-agent.cli]  kAwait1 enter: append=%d\n", append_ ? 1 : 0);

              if (!append_) {
                Fail("internal error");
                return dust::Poll<void>::Ready();
              }

              auto polled = append_->PollOnce(ctx);
              if (polled.is_pending()) {
                if (DebugAgentCli())
                  std::fprintf(stderr, "[cpp-agent.cli]  kAwait1 PollOnce -> Pending\n");
                return dust::Poll<void>::Pending();
              }

              if (DebugAgentCli())
                std::fprintf(stderr, "[cpp-agent.cli]  kAwait1 PollOnce -> Ready\n");

              auto r = polled.TakeReady();
              append_ = nullptr;
              if (!r.ok()) {
                Fail(r.error());
                return dust::Poll<void>::Ready();
              }

              agent::Agent* a = runner_->agent();
              if (!a) {
                Fail("missing agent");
                return dust::Poll<void>::Ready();
              }

              run_ = a->Run(ctx_);
              if (DebugAgentCli())
                std::fprintf(stderr, "[cpp-agent.cli]  kAwait1 after Run: run=%d\n", run_ ? 1 : 0);

              if (!run_) {
                Fail("internal error");
                return dust::Poll<void>::Ready();
              }

              state_ = State::kAwait2;
              break;
            }

            case State::kAwait2: {
              if (DebugAgentCli())
                std::fprintf(stderr, "[cpp-agent.cli]  kAwait2 enter: run=%d\n", run_ ? 1 : 0);

              if (!run_) {
                Fail("internal error");
                return dust::Poll<void>::Ready();
              }

              auto polled = run_->PollOnce(ctx);
              if (polled.is_pending()) {
                if (DebugAgentCli())
                  std::fprintf(stderr, "[cpp-agent.cli]  kAwait2 PollOnce -> Pending\n");
                return dust::Poll<void>::Pending();
              }

              if (DebugAgentCli())
                std::fprintf(stderr, "[cpp-agent.cli]  kAwait2 PollOnce -> Ready\n");

              auto r = polled.TakeReady();
              run_ = nullptr;
              if (!r.ok()) {
                Fail(r.error());
                return dust::Poll<void>::Ready();
              }

              dust::RefPtr<agent::ChatMessage> last = ctx_->history()->GetLast(ctx_);
              if (!last || last->role() != agent::ChatRole::kAssistant || !last->content()) {
                Fail("no assistant reply");
                return dust::Poll<void>::Ready();
              }

              if (last->content()->kind() != agent::ChatContent::Kind::kText) {
                Fail("assistant reply is not text");
                return dust::Poll<void>::Ready();
              }

              auto* text = static_cast<const agent::ChatContent::Text*>(last->content());
              std::cout << text->text() << "\n";
              Succeed();
              return dust::Poll<void>::Ready();
            }

            case State::kDone:
              break;
          }
        }

        if (DebugAgentCli())
          std::fprintf(stderr, "[cpp-agent.cli]  kDone\n");
        return dust::Poll<void>::Ready();

        return dust::Poll<void>::Ready();
      }

     private:
      enum class State { kInit, kAwait1, kAwait2, kDone };

      void Succeed() {
        done_ = true;
        state_ = State::kDone;
        loop_->Quit();
      }

      void Fail(std::string msg) {
        std::cerr << "error: " << msg << "\n";
        done_ = true;
        state_ = State::kDone;
        loop_->Quit();
      }

      agent::AgentRunner* runner_ = nullptr;
      dust::RefPtr<agent::AgentContext> ctx_;
      dust::MessageLoop* loop_ = nullptr;
      std::string input_;

      bool done_ = false;
      State state_ = State::kInit;
      dust::FuturePtr<dust::Result<void, std::string>> append_;
      dust::FuturePtr<dust::Result<void, std::string>> run_;
    };

    auto f = dust::MakeRefPtr<RunOnceFuture>(&runner, ctx, &loop, std::move(input));
    loop.executor()->Spawn(std::move(f));

    loop.Run();
    return 0;
  }

  auto console = std::make_unique<agent::CliConsole>();

  // Bind runner <-> console before starting IO.
  runner.Run(session, std::move(console));

  // CliConsole watches stdin on MessageLoop::Current().
  // MessageLoop must exist before Start().
  auto* cli = static_cast<agent::CliConsole*>(runner.console());
  std::cout << "ready.\n";


  cli->Start();

  loop.Run();
  return 0;
}
