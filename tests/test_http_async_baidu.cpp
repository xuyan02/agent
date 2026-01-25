#include "infra/http_async/async_client.h"

#include <cassert>
#include <cstdlib>
#include <memory>

#include "dust/message_loop/linux_message_pump_epoll.h"
#include "dust/message_loop/message_loop.h"
#include "dust/time/duration.h"

#include <utility>

int main() {
  // NOTE: This is a network integration test. It may fail if:
  // - no internet access
  // - DNS is blocked
  // - https MITM/captive portal
  // - baidu is blocked
  // If needed, skip by setting CPP_AGENT_SKIP_NET_TESTS=1.
  if (const char* skip = std::getenv("CPP_AGENT_SKIP_NET_TESTS")) {
    if (*skip == '1') return 0;
  }

  auto pump = std::make_unique<dust::LinuxMessagePumpEpoll>();
  dust::MessageLoop loop(std::move(pump));

  std::unique_ptr<http::AsyncClient> client;
  int rc = 1;

  loop.task_runner()->PostTask(dust::OnceClosure([&] {
    client = std::make_unique<http::AsyncClient>();

    http::Request req;
    req.method = "GET";
    req.url = "https://www.baidu.com/";
    req.follow_redirects = true;
    req.max_redirects = 5;
    req.timeout_ms = 5000;

    client->Start(std::move(req), dust::OnceFunction<void(http::Result)>([&, loop_ptr = &loop](http::Result res) {
      // Many environments may return 200 (OK) or 301/302 before follow.
      // With follow_redirects=true, expect final 200.
      if (res.error.code != http::ErrorCode::kOk) {
        rc = 2;
        loop_ptr->QuitWhenIdle();
        return;
      }

      if (res.response.status != 200) {
        rc = 3;
        loop_ptr->QuitWhenIdle();
        return;
      }

      // Basic sanity: HTML-ish body.
      if (res.response.body.empty()) {
        rc = 4;
        loop_ptr->QuitWhenIdle();
        return;
      }

      rc = 0;
      loop_ptr->QuitWhenIdle();
    }));
  }));

  // Hard timeout for the loop, avoid hanging CI.
  auto* loop_ptr = &loop;
  loop.task_runner()->PostDelayedTask(
      dust::Duration::FromMilliseconds(8000),
      dust::OnceClosure([loop_ptr, &rc] {
        if (rc != 0) rc = 5;
        loop_ptr->QuitWhenIdle();
      }));

  loop.Run();
  return rc;
}
