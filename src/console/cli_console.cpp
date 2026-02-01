#include "console/cli_console.h"

#include "dust/message_loop/watch_callbacks.h"

#include <cstdlib>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <string.h>
#include <unistd.h>

namespace agent {
namespace {

constexpr int kStdinFd = 0;

} // namespace

void CliConsole::PrintLine(const std::string& s) {
  std::cout << s << "\n";
  std::cout.flush();
}

void CliConsole::Print(const std::string& s) {
  std::cout << s;
  std::cout.flush();
}

namespace {

bool DebugConsole() {
  const char* v = std::getenv("CPP_AGENT_DEBUG_CONSOLE");
  return v && *v && strcmp(v, "0") != 0;
}

} // namespace

void CliConsole::SetOnLine(dust::Function<void(std::string)> on_line) { on_line_ = std::move(on_line); }

void CliConsole::AttachToMessageLoop(dust::MessageLoop& loop) {
  loop_ = &loop;

  const int flags = fcntl(kStdinFd, F_GETFL, 0);
  if (flags >= 0 && (flags & O_NONBLOCK) == 0) {
    (void)fcntl(kStdinFd, F_SETFL, flags | O_NONBLOCK);
  }

  if (DebugConsole()) {
    std::cerr << "[cpp-agent.console] attach stdin fd=0 (nonblock="
              << ((fcntl(kStdinFd, F_GETFL, 0) & O_NONBLOCK) != 0) << ")" << std::endl;
  }

  dust::WatchCallbacks cb;
  cb.on_readable = [this]() { OnStdinReadable(); };
  loop.WatchFd(kStdinFd, std::move(cb));
}

void CliConsole::OnStdinReadable() {
  if (!loop_) return;

  // Avoid starving the message loop: cap how much we drain per readability event.
  // This prevents a flood/paste from keeping us inside this callback for too long,
  // which would delay timers, eventfd wakeups, and network socket progress.
  constexpr int kMaxReadsPerTick = 4096;
  constexpr size_t kMaxBytesPerTick = 16 * 1024;


  size_t bytes = 0;
  char buf[4096];
  for (int reads = 0; reads < kMaxReadsPerTick && bytes < kMaxBytesPerTick; ++reads) {
    const ssize_t n = read(kStdinFd, buf, sizeof(buf));


    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return;
      }
      loop_->UnwatchFd(kStdinFd);
      return;
    }

    if (n == 0) {
      loop_->UnwatchFd(kStdinFd);
      return;
    }

    bytes += static_cast<size_t>(n);
    buffer_.append(buf, buf + n);

    for (;;) {
      auto pos = buffer_.find('\n');
      if (pos == std::string::npos) break;
      std::string line = buffer_.substr(0, pos);
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      buffer_.erase(0, pos + 1);
      if (DebugConsole()) {
        std::cerr << "[cpp-agent.console] on_line: " << line << std::endl;
	std::cerr << "[cpp-agent.console] buffer: " << buffer_.size() << std::endl;
      }
      if (on_line_) on_line_(std::move(line));
    }

    if (n < static_cast<ssize_t>(sizeof(buf))) break;
  }

}

} // namespace agent
