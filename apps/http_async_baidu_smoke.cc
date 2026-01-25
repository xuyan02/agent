#include "infra/http_async/async_client.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "dust/message_loop/linux_message_pump_epoll.h"
#include "dust/message_loop/message_loop.h"
#include "dust/time/duration.h"

namespace {

std::string EnvOrDefault(const char* key, const char* def) {
  const char* v = std::getenv(key);
  return (v && *v) ? std::string(v) : std::string(def);
}

int EnvIntOrDefault(const char* key, int def) {
  const char* v = std::getenv(key);
  if (!v || !*v) return def;
  return std::atoi(v);
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  const std::string url = EnvOrDefault("CPP_AGENT_SMOKE_URL", "https://www.baidu.com/");
  const int timeout_ms = EnvIntOrDefault("CPP_AGENT_SMOKE_TIMEOUT_MS", 5000);

  std::printf("[smoke] url=%s timeout_ms=%d\n", url.c_str(), timeout_ms);

  auto pump = std::make_unique<dust::LinuxMessagePumpEpoll>();
  dust::MessageLoop loop(std::move(pump));

  std::unique_ptr<http::AsyncClient> client;
  int rc = 1;

  auto* loop_ptr = &loop;
  loop.task_runner()->PostTask(dust::OnceClosure([&, loop_ptr] {
    client = std::make_unique<http::AsyncClient>();

    http::Request req;
    req.method = "GET";
    req.url = url;
    req.follow_redirects = true;
    req.max_redirects = 5;
    req.timeout_ms = timeout_ms;

    client->Start(std::move(req), dust::OnceFunction<void(http::Result)>(
                                    [&, loop_ptr = &loop](http::Result res) {
                                      if (res.error.code != http::ErrorCode::kOk) {
                                        std::printf("[smoke] error: code=%d msg=%s\n",
                                                    static_cast<int>(res.error.code),
                                                    res.error.message.c_str());
                                        rc = 2;
                                        loop_ptr->QuitWhenIdle();
                                        return;
                                      }

                                      std::printf("[smoke] http status=%ld\n", res.response.status);
                                      std::printf("[smoke] body_bytes=%zu\n", res.response.body.size());
                                      if (!res.response.body.empty()) {
                                        const size_t n = res.response.body.size() < 200
                                                             ? res.response.body.size()
                                                             : 200;
                                        std::printf("[smoke] body_prefix(%zu):\n%.*s\n", n,
                                                    static_cast<int>(n),
                                                    res.response.body.data());
                                      }

                                      rc = (res.response.status == 200 && !res.response.body.empty())
                                               ? 0
                                               : 3;
                                      loop_ptr->QuitWhenIdle();
                                    }));
  }));

  loop.task_runner()->PostDelayedTask(
      dust::Duration::FromMilliseconds(timeout_ms + 3000),
      dust::OnceClosure([loop_ptr, &rc] {
        if (rc != 0) {
          std::printf("[smoke] timeout waiting for request to finish\n");
          rc = 4;
        }
        loop_ptr->QuitWhenIdle();
      }));

  loop.Run();
  return rc;
}
