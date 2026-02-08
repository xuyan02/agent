#include "dust/message_loop/linux_message_pump_epoll.h"
#include "dust/message_loop/message_loop.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace {

volatile sig_atomic_t g_timed_out = 0;

void OnAlarm(int) {
  g_timed_out = 1;
}

}  // namespace

int main() {
  // Exit quickly even if stdin never becomes readable.
  std::signal(SIGALRM, &OnAlarm);
  alarm(1);

  auto pump = std::make_unique<dust::LinuxMessagePumpEpoll>();
  dust::MessageLoop loop(std::move(pump));

  bool readable_called = false;

  dust::WatchCallbacks cb;
  cb.on_readable = [&]() {
    readable_called = true;
    std::fprintf(stderr, "[watch-stdin] readable callback fired\n");
    loop.Quit();
  };

  std::fprintf(stderr, "[watch-stdin] attempting WatchFd(0)\n");
  loop.WatchFd(0, std::move(cb));
  std::fprintf(stderr, "[watch-stdin] WatchFd(0) returned\n");

  while (!g_timed_out && !readable_called) {
    loop.RunOnce();
  }

  if (g_timed_out && !readable_called) {
    std::fprintf(stderr, "[watch-stdin] timeout (watch may still be installed, but no event)\n");
  }

  return 0;
}
