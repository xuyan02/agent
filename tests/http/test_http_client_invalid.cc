#include "http/http_client.h"

#include "dust/message_loop/linux_message_pump_epoll.h"
#include "dust/message_loop/message_loop.h"

#include <cassert>
#include <cstdio>

namespace agent {
namespace {

void TestHttpClientInvalidUrl() {
  dust::MessageLoop loop(std::make_unique<dust::LinuxMessagePumpEpoll>());

  HttpClient client;

  HttpRequest req;
  req.method = "GET";
  req.url = "";  // invalid

  auto f = client.Send(std::move(req));
  assert(f);

  // Deterministic: invalid url resolves synchronously.
  dust::PollContext ctx{dust::WakerHandle()};
  auto polled = f->PollOnce(ctx);
  assert(polled.is_ready());

  auto r = polled.TakeReady();
  assert(!r.ok());
  assert(r.error().code == HttpErrorCode::kInvalidArgument);
  assert(!r.error().message.empty());
}

}  // namespace
}  // namespace agent

int main() {
  agent::TestHttpClientInvalidUrl();
  std::printf("ok\n");
  return 0;
}
