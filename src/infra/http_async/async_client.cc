#include "infra/http_async/async_client.h"

#include <curl/curl.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "dust/message_loop/message_loop.h"
#include "dust/message_loop/watch_callbacks.h"
#include "dust/time/duration.h"

namespace http {
namespace {

bool DebugHttp() {
  const char* v = std::getenv("CPP_AGENT_DEBUG_HTTP");
  return v && *v && std::strcmp(v, "0") != 0;
}

const char* CurlPollToStr(int what) {
  static thread_local char buf[64];
  buf[0] = '\0';
  if (what == CURL_POLL_REMOVE)
    return "REMOVE";
  if (what & CURL_POLL_IN)
    std::strcat(buf, "IN|");
  if (what & CURL_POLL_OUT)
    std::strcat(buf, "OUT|");
  if (what & CURL_POLL_NONE)
    std::strcat(buf, "NONE|");
  if (buf[0] == '\0')
    return "0";
  size_t n = std::strlen(buf);
  if (n && buf[n - 1] == '|')
    buf[n - 1] = '\0';
  return buf;
}

int CurlWhatToActions(int what) {
  int actions = 0;
  if (what & CURL_POLL_IN)
    actions |= CURL_CSELECT_IN;
  if (what & CURL_POLL_OUT)
    actions |= CURL_CSELECT_OUT;
  if (what & CURL_POLL_INOUT)
    actions |= (CURL_CSELECT_IN | CURL_CSELECT_OUT);
  return actions;
}

class ClientImpl;

static size_t WriteHeaderCb(char* ptr, size_t size, size_t nmemb, void* userdata);
static size_t WriteBodyCb(char* ptr, size_t size, size_t nmemb, void* userdata);
static int SocketCb(CURL* /*easy*/, curl_socket_t s, int what, void* userp, void* /*socketp*/);
static int TimerCb(CURLM* /*multi*/, long timeout_ms, void* userp);

class ClientImpl {
 public:
  ClientImpl(Request req, OnceCallback on_finish)
      : req_(std::move(req)), on_finish_(std::move(on_finish)) {
    loop_ = dust::MessageLoop::Current();
    if (!loop_) {
      std::fprintf(stderr, "AsyncClient requires a MessageLoop bound to this thread\n");
      std::abort();
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    multi_ = curl_multi_init();
    if (!multi_) {
      std::fprintf(stderr, "curl_multi_init failed\n");
      std::abort();
    }

    (void)curl_multi_setopt(multi_, CURLMOPT_SOCKETFUNCTION, &SocketCb);
    (void)curl_multi_setopt(multi_, CURLMOPT_SOCKETDATA, this);
    (void)curl_multi_setopt(multi_, CURLMOPT_TIMERFUNCTION, &TimerCb);
    (void)curl_multi_setopt(multi_, CURLMOPT_TIMERDATA, this);

    Start();
  }

  ~ClientImpl() {
    // A/B semantics: destruction cancels silently; never invoke finish.
    alive_.store(false);
    CancelAndTeardown();

    if (multi_)
      curl_multi_cleanup(multi_);
    multi_ = nullptr;

    curl_global_cleanup();
  }

  void OnHeader(std::string name, std::string value) {
    if (done_.load())
      return;
    result_.response.headers.push_back(Header{std::move(name), std::move(value)});
  }

  size_t OnBody(const char* data, size_t n) {
    if (done_.load())
      return 0;

    if (req_.on_body_chunk) {
      const bool ok = req_.on_body_chunk(data, n);
      if (!ok)
        return 0;  // abort
    }

    result_.response.body.append(data, n);
    return n;
  }

  int OnSocket(int fd, int what) {
    if (!alive_.load())
      return 0;

    if (DebugHttp()) {
      std::fprintf(stderr, "[cpp-agent.http] curl socket_cb fd=%d what=%s raw=%d\n", fd,
                   CurlPollToStr(what), what);
    }

    if (what == CURL_POLL_REMOVE) {
      loop_->UnwatchFd(fd);
      fd_actions_.erase(fd);
      return 0;
    }

    const int actions = CurlWhatToActions(what);
    fd_actions_[fd] = actions;

    dust::WatchCallbacks cbs;
    cbs.on_readable = dust::Closure([this, fd]() { Pump(fd, CURL_CSELECT_IN); });
    cbs.on_writable = dust::Closure([this, fd]() { Pump(fd, CURL_CSELECT_OUT); });
    cbs.on_error = dust::Closure([this, fd]() { Pump(fd, CURL_CSELECT_ERR); });

    loop_->WatchFd(fd, std::move(cbs));
    return 0;
  }

  int OnTimer(long timeout_ms) {
    if (!alive_.load())
      return 0;

    if (DebugHttp()) {
      std::fprintf(stderr, "[cpp-agent.http] curl timer_cb timeout_ms=%ld\n", timeout_ms);
    }

    if (timeout_ms < 0) {
      timer_gen_.fetch_add(1);
      return 0;
    }

    const uint64_t gen = timer_gen_.fetch_add(1) + 1;
    loop_->task_runner()->PostDelayedTask(
        dust::Duration::FromMilliseconds(timeout_ms),
        dust::OnceClosure([this, gen]() {
          if (!alive_.load())
            return;
          if (timer_gen_.load() != gen)
            return;
          Pump(CURL_SOCKET_TIMEOUT, 0);
        }));

    return 0;
  }

 private:
  void Start() {
    if (req_.url.empty() || !on_finish_) {
      done_.store(true);
      return;
    }

    easy_ = curl_easy_init();
    if (!easy_) {
      result_.error.code = ErrorCode::kCurl;
      result_.error.message = "curl_easy_init failed";
      FinishNow();
      return;
    }

    curl_easy_setopt(easy_, CURLOPT_URL, req_.url.c_str());
    curl_easy_setopt(easy_, CURLOPT_CUSTOMREQUEST, req_.method.c_str());

    for (const auto& h : req_.headers) {
      std::string line = h.name + ": " + h.value;
      req_headers_ = curl_slist_append(req_headers_, line.c_str());
    }
    if (req_headers_)
      curl_easy_setopt(easy_, CURLOPT_HTTPHEADER, req_headers_);

    if (!req_.body.empty()) {
      curl_easy_setopt(easy_, CURLOPT_POSTFIELDS, req_.body.c_str());
      curl_easy_setopt(easy_, CURLOPT_POSTFIELDSIZE, static_cast<long>(req_.body.size()));
    }

    curl_easy_setopt(easy_, CURLOPT_FOLLOWLOCATION, req_.follow_redirects ? 1L : 0L);
    curl_easy_setopt(easy_, CURLOPT_MAXREDIRS, static_cast<long>(req_.max_redirects));

    if (req_.timeout_ms.has_value())
      curl_easy_setopt(easy_, CURLOPT_TIMEOUT_MS, static_cast<long>(*req_.timeout_ms));

    curl_easy_setopt(easy_, CURLOPT_WRITEFUNCTION, &WriteBodyCb);
    curl_easy_setopt(easy_, CURLOPT_WRITEDATA, this);
    curl_easy_setopt(easy_, CURLOPT_HEADERFUNCTION, &WriteHeaderCb);
    curl_easy_setopt(easy_, CURLOPT_HEADERDATA, this);

    CURLMcode mrc = curl_multi_add_handle(multi_, easy_);
    if (mrc != CURLM_OK) {
      result_.error.code = ErrorCode::kCurl;
      result_.error.message = curl_multi_strerror(mrc);
      CancelAndTeardown();
      FinishNow();
      return;
    }

    Pump(CURL_SOCKET_TIMEOUT, 0);
  }

  void Pump(curl_socket_t s, int ev_bitmask) {
    if (!alive_.load() || done_.load())
      return;

    if (DebugHttp()) {
      std::fprintf(stderr, "[cpp-agent.http] PumpMulti fd=%d ev_bitmask=0x%x\n", static_cast<int>(s),
                   ev_bitmask);
    }

    int running = 0;
    (void)curl_multi_socket_action(multi_, s, ev_bitmask, &running);
    DrainCompletions();
  }

  void DrainCompletions() {
    if (!alive_.load() || done_.load())
      return;

    int msgs = 0;
    while (CURLMsg* msg = curl_multi_info_read(multi_, &msgs)) {
      if (msg->msg != CURLMSG_DONE)
        continue;
      if (msg->easy_handle != easy_)
        continue;

      long status = 0;
      curl_easy_getinfo(easy_, CURLINFO_RESPONSE_CODE, &status);
      result_.response.status = status;

      const CURLcode code = msg->data.result;
      if (code != CURLE_OK) {
        result_.error.code = ErrorCode::kCurl;
        result_.error.curl_code = static_cast<int>(code);
        result_.error.message = curl_easy_strerror(code);
      }

      CancelAndTeardown();
      FinishNow();
      return;
    }
  }

  void CancelAndTeardown() {
    if (easy_) {
      (void)curl_multi_remove_handle(multi_, easy_);
      curl_easy_cleanup(easy_);
      easy_ = nullptr;
    }

    if (req_headers_) {
      curl_slist_free_all(req_headers_);
      req_headers_ = nullptr;
    }

    for (const auto& kv : fd_actions_) {
      loop_->UnwatchFd(kv.first);
    }
    fd_actions_.clear();

    timer_gen_.fetch_add(1);
  }

  void FinishNow() {
    if (done_.exchange(true))
      return;
    if (!alive_.load())
      return;

    if (on_finish_)
      std::move(on_finish_)(std::move(result_));

    req_.on_body_chunk = dust::Function<bool(const char*, size_t)>();
    on_finish_ = dust::OnceFunction<void(http::Result)>();
  }

 private:
  dust::MessageLoop* loop_ = nullptr;

  CURLM* multi_ = nullptr;
  CURL* easy_ = nullptr;
  curl_slist* req_headers_ = nullptr;

  Request req_;
  OnceCallback on_finish_;
  Result result_;

  std::unordered_map<int, int> fd_actions_;
  std::atomic<uint64_t> timer_gen_{0};

  std::atomic<bool> alive_{true};
  std::atomic<bool> done_{false};
};

static size_t WriteHeaderCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  const size_t n = size * nmemb;
  auto* self = static_cast<ClientImpl*>(userdata);
  if (!self)
    return n;

  std::string line(ptr, n);
  auto colon = line.find(':');
  if (colon == std::string::npos)
    return n;

  std::string name = line.substr(0, colon);
  size_t i = colon + 1;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
    i++;
  std::string value = line.substr(i);
  while (!value.empty() && (value.back() == '\r' || value.back() == '\n'))
    value.pop_back();

  self->OnHeader(std::move(name), std::move(value));
  return n;
}

static size_t WriteBodyCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  const size_t n = size * nmemb;
  auto* self = static_cast<ClientImpl*>(userdata);
  if (!self)
    return n;
  return self->OnBody(ptr, n);
}

static int SocketCb(CURL* /*easy*/, curl_socket_t s, int what, void* userp, void* /*socketp*/) {
  auto* self = static_cast<ClientImpl*>(userp);
  if (!self)
    return 0;
  return self->OnSocket(static_cast<int>(s), what);
}

static int TimerCb(CURLM* /*multi*/, long timeout_ms, void* userp) {
  auto* self = static_cast<ClientImpl*>(userp);
  if (!self)
    return 0;
  return self->OnTimer(timeout_ms);
}

}  // namespace

class AsyncClient::Impl final : public ClientImpl {
 public:
  Impl(Request req, OnceCallback on_finish) : ClientImpl(std::move(req), std::move(on_finish)) {}
};

AsyncClient::AsyncClient(Request req, OnceCallback on_finish)
    : impl_(std::make_unique<Impl>(std::move(req), std::move(on_finish))) {}

AsyncClient::~AsyncClient() = default;

}  // namespace http
