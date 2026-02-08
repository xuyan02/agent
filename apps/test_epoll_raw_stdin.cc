#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>

namespace {

void PrintResult(const char* op, int rc) {
  if (rc == 0) {
    std::fprintf(stderr, "[raw-epoll] %s: ok\n", op);
    return;
  }
  const int e = errno;
  std::fprintf(stderr, "[raw-epoll] %s: rc=%d errno=%d (%s)\n", op, rc, e, std::strerror(e));
}

}  // namespace

int main() {
  const int orig_flags = fcntl(0, F_GETFL, 0);
  if (orig_flags < 0) {
    PrintResult("fcntl(F_GETFL)", -1);
  } else {
    std::fprintf(stderr, "[raw-epoll] stdin flags before: 0x%x\n", orig_flags);
  }

  if (orig_flags >= 0) {
    const int rc = fcntl(0, F_SETFL, orig_flags | O_NONBLOCK);
    PrintResult("fcntl(F_SETFL|O_NONBLOCK)", rc);
  }

  const int ep = epoll_create1(0);
  if (ep < 0) {
    PrintResult("epoll_create1", -1);
    return 2;
  }

  epoll_event ev;
  std::memset(&ev, 0, sizeof(ev));
  ev.events = EPOLLIN;
  ev.data.fd = 0;

  std::fprintf(stderr, "[raw-epoll] trying EPOLL_CTL_ADD fd=0\n");
  PrintResult("epoll_ctl(ADD)", epoll_ctl(ep, EPOLL_CTL_ADD, 0, &ev));

  std::fprintf(stderr, "[raw-epoll] trying EPOLL_CTL_MOD fd=0\n");
  PrintResult("epoll_ctl(MOD)", epoll_ctl(ep, EPOLL_CTL_MOD, 0, &ev));

  close(ep);
  return 0;
}
