#include "http/http_client.h"

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

namespace agent {
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

HttpError MakeCurlError(CURLcode code, std::string msg_prefix = "curl") {
  HttpError e;
  e.code = HttpErrorCode::kCurl;
  e.curl_code = static_cast<int>(code);
  e.message = msg_prefix + ": " + curl_easy_strerror(code);
  return e;
}

class RequestFuture;

static size_t WriteHeaderCb(char* ptr, size_t size, size_t nmemb, void* userdata);
static size_t WriteBodyCb(char* ptr, size_t size, size_t nmemb, void* userdata);
static int SocketCb(CURL* /*easy*/, curl_socket_t s, int what, void* userp, void* /*socketp*/);
static int TimerCb(CURLM* /*multi*/, long timeout_ms, void* userp);

class RequestFuture final : public dust::Future<dust::Result<HttpResponse, HttpError>> {
 public:
  RequestFuture() = default;

  explicit RequestFuture(HttpRequest req) : req_(std::move(req)) {}

  ~RequestFuture() override {
    // Best-effort cancel; do not call callbacks.
    alive_ = false;
    CancelAndTeardown();

    if (multi_)
      curl_multi_cleanup(multi_);
    multi_ = nullptr;
  }

  dust::Poll<dust::Result<HttpResponse, HttpError>> PollOnce(dust::PollContext& ctx) override {
    if (done_)
      return TakeDone();

    if (!started_) {
      started_ = true;
      // Note: RequestFuture is a low-level IO future and is allowed to store a weak WakerHandle.
      waker_ = ctx.waker();

      loop_ = dust::MessageLoop::Current();
      if (DebugHttp()) {
        std::fprintf(stderr, "[cpp-agent.http2] Subscribe CurrentLoop=%p\n", (void*)loop_);
      }
      if (!loop_) {
        done_ = true;
        return dust::Poll<dust::Result<HttpResponse, HttpError>>::Ready(
            dust::Result<HttpResponse, HttpError>::Err(
                HttpError{HttpErrorCode::kInvalidArgument, 0, "HttpClient requires MessageLoop"}));
      }

      // Initialize curl globally early; do not pair with curl_global_cleanup() here.
      // libcurl documents it as process-global and not safe to init/cleanup per-request.
      curl_global_init(CURL_GLOBAL_DEFAULT);

      if (req_.url.empty()) {
        done_ = true;
        return dust::Poll<dust::Result<HttpResponse, HttpError>>::Ready(
            dust::Result<HttpResponse, HttpError>::Err(
                HttpError{HttpErrorCode::kInvalidArgument, 0, "empty url"}));
      }

      multi_ = curl_multi_init();
      if (!multi_) {
        done_ = true;
        return dust::Poll<dust::Result<HttpResponse, HttpError>>::Ready(
            dust::Result<HttpResponse, HttpError>::Err(
                HttpError{HttpErrorCode::kCurl, 0, "curl_multi_init failed"}));
      }

      (void)curl_multi_setopt(multi_, CURLMOPT_SOCKETFUNCTION, &SocketCb);
      (void)curl_multi_setopt(multi_, CURLMOPT_SOCKETDATA, this);
      (void)curl_multi_setopt(multi_, CURLMOPT_TIMERFUNCTION, &TimerCb);
      (void)curl_multi_setopt(multi_, CURLMOPT_TIMERDATA, this);

      Start();
      if (done_)
        return TakeDone();

      if (DebugHttp()) {
        std::fprintf(stderr, "[cpp-agent.http2] started: Pending (loop=%p)\n", (void*)loop_);
      }

      return dust::Poll<dust::Result<HttpResponse, HttpError>>::Pending();
    }

    if (io_ready_) {
      io_ready_ = false;
      if (done_)
        return TakeDone();
    }

    return dust::Poll<dust::Result<HttpResponse, HttpError>>::Pending();
  }

  void OnHeader(std::string name, std::string value) {
    if (done_)
      return;
    response_.headers.push_back(HttpHeader{std::move(name), std::move(value)});
  }

  size_t OnBody(const char* data, size_t n) {
    if (done_)
      return 0;

    if (req_.on_body_chunk) {
      const bool ok = req_.on_body_chunk(data, n);
      if (!ok)
        return 0;
    }

    response_.body.append(data, n);
    return n;
  }

  int OnSocket(int fd, int what) {
    if (!alive_)
      return 0;

    if (DebugHttp()) {
      std::fprintf(stderr, "[cpp-agent.http2] curl socket_cb fd=%d what=%s raw=%d\n", fd,
                   CurlPollToStr(what), what);
    }

    if (what == CURL_POLL_REMOVE) {
      loop_->UnwatchFd(fd);
      fd_actions_.erase(fd);
      return 0;
    }

    const int actions = CurlWhatToActions(what);
    // curl may request OUT while connecting; once connected it will usually switch to IN.
    // We rely on this to avoid EPOLLOUT wake storms.
    fd_actions_[fd] = actions;

    dust::WatchCallbacks cbs;
    cbs.on_readable = dust::Closure([this, fd]() {
      io_ready_ = true;
      const int actions = fd_actions_.count(fd) ? fd_actions_[fd] : 0;
      if (!(actions & CURL_POLL_IN)) {
        if (DebugHttp())
          std::fprintf(stderr, "[cpp-agent.http2] fd readable: ignored (actions=0x%x)\n",
                       actions);
        return;
      }
      if (DebugHttp())
        std::fprintf(stderr, "[cpp-agent.http2] fd readable: Wake (actions=0x%x)\n", actions);
      Pump(fd, CURL_CSELECT_IN);
      waker_.Wake();
    });
    cbs.on_writable = dust::Closure([this, fd]() {
      io_ready_ = true;
      const int actions = fd_actions_.count(fd) ? fd_actions_[fd] : 0;
      if (!(actions & CURL_POLL_OUT)) {
        if (DebugHttp())
          std::fprintf(stderr, "[cpp-agent.http2] fd writable: ignored (actions=0x%x)\n",
                       actions);
        return;
      }

      // IMPORTANT: a socket can remain level-triggered writable for long periods. Calling Wake()
      // here would spin the executor. Instead, just notify curl; it will request new interests via
      // socket_cb (which updates WatchFd) and/or arm timers via timer_cb.
      if (DebugHttp())
        std::fprintf(stderr, "[cpp-agent.http2] fd writable: Pump (actions=0x%x)\n", actions);
      Pump(fd, CURL_CSELECT_OUT);
    });
    cbs.on_error = dust::Closure([this, fd]() {
      io_ready_ = true;
      const int actions = fd_actions_.count(fd) ? fd_actions_[fd] : 0;
      if (DebugHttp())
        std::fprintf(stderr, "[cpp-agent.http2] fd error: Wake (actions=0x%x)\n", actions);
      Pump(fd, CURL_CSELECT_ERR);
      waker_.Wake();
    });

    // MessageLoop computes epoll interests from which callbacks are non-empty.
    // Respect curl's requested directions by only installing the requested callbacks.
    if (!(actions & CURL_POLL_IN))
      cbs.on_readable = dust::Closure();
    if (!(actions & CURL_POLL_OUT))
      cbs.on_writable = dust::Closure();

    loop_->WatchFd(fd, std::move(cbs));
    if (DebugHttp()) {
      std::fprintf(stderr, "[cpp-agent.http2] WatchFd fd=%d actions=0x%x\n", fd, actions);
    }
    return 0;
  }

  int OnTimer(long timeout_ms) {
    if (!alive_)
      return 0;

    if (DebugHttp()) {
      std::fprintf(stderr, "[cpp-agent.http2] curl timer_cb timeout_ms=%ld\n", timeout_ms);
    }

    if (timeout_ms < 0) {
      ++timer_gen_;
      return 0;
    }

    const uint64_t gen = ++timer_gen_;
    if (DebugHttp()) {
      std::fprintf(stderr, "[cpp-agent.http2] PostDelayedTask timeout_ms=%ld gen=%llu\n", timeout_ms,
                   static_cast<unsigned long long>(gen));
    }

    loop_->task_runner()->PostDelayedTask(
        dust::Duration::FromMilliseconds(timeout_ms),
        dust::OnceClosure([this, gen]() {
          if (!alive_) {
            if (DebugHttp())
              std::fprintf(stderr, "[cpp-agent.http2] timer fire: drop (dead) gen=%llu\n",
                           static_cast<unsigned long long>(gen));
            return;
          }
          if (timer_gen_ != gen) {
            if (DebugHttp())
              std::fprintf(stderr,
                           "[cpp-agent.http2] timer fire: drop (stale) gen=%llu cur=%llu\n",
                           static_cast<unsigned long long>(gen),
                           static_cast<unsigned long long>(timer_gen_));
            return;
          }
          io_ready_ = true;
          if (DebugHttp())
            std::fprintf(stderr, "[cpp-agent.http2] timer fire: Pump+Wake gen=%llu\n",
                         static_cast<unsigned long long>(gen));
          Pump(CURL_SOCKET_TIMEOUT, 0);
          waker_.Wake();
        }));

    return 0;
  }

 private:
  void Start() {
    if (DebugHttp())
      std::fprintf(stderr, "[cpp-agent.http2] Start url=%s\n", req_.url.c_str());

    easy_ = curl_easy_init();
    if (!easy_) {
      SetDone(dust::Result<HttpResponse, HttpError>::Err(
          HttpError{HttpErrorCode::kCurl, 0, "curl_easy_init failed"}));
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
    if (DebugHttp()) {
      std::fprintf(stderr, "[cpp-agent.http2] curl_multi_add_handle -> %d (%s)\n", static_cast<int>(mrc),
                   curl_multi_strerror(mrc));
    }
    if (mrc != CURLM_OK) {
      SetDone(dust::Result<HttpResponse, HttpError>::Err(
          HttpError{HttpErrorCode::kCurl, 0, curl_multi_strerror(mrc)}));
      CancelAndTeardown();
      return;
    }

    Pump(CURL_SOCKET_TIMEOUT, 0);
  }

  void Pump(curl_socket_t s, int ev_bitmask) {
    if (!alive_ || done_)
      return;

    if (DebugHttp()) {
      std::fprintf(stderr, "[cpp-agent.http2] PumpMulti fd=%d ev_bitmask=0x%x\n", static_cast<int>(s),
                   ev_bitmask);
    }

    int running = 0;
    CURLMcode mrc = curl_multi_socket_action(multi_, s, ev_bitmask, &running);
    if (DebugHttp()) {
      std::fprintf(stderr, "[cpp-agent.http2] curl_multi_socket_action -> %d (%s) running=%d\n",
                   static_cast<int>(mrc), curl_multi_strerror(mrc), running);
    }
    DrainCompletions();
  }

  void DrainCompletions() {
    if (!alive_ || done_)
      return;

    if (DebugHttp())
      std::fprintf(stderr, "[cpp-agent.http2] DrainCompletions\n");

    int msgs = 0;
    while (CURLMsg* msg = curl_multi_info_read(multi_, &msgs)) {
      if (DebugHttp()) {
        const char* msg_name = "?";
        if (msg->msg == CURLMSG_DONE)
          msg_name = "DONE";
        std::fprintf(stderr,
                     "[cpp-agent.http2] info_read msg=%s easy=%p self_easy=%p result=%d\n",
                     msg_name,
                     msg->easy_handle,
                     easy_,
                     msg->data.result);
      }

      if (msg->msg != CURLMSG_DONE)
        continue;
      if (msg->easy_handle != easy_)
        continue;

      long status = 0;
      curl_easy_getinfo(easy_, CURLINFO_RESPONSE_CODE, &status);
      response_.status = status;

      const CURLcode code = msg->data.result;
      if (code != CURLE_OK) {
        SetDone(dust::Result<HttpResponse, HttpError>::Err(MakeCurlError(code)));
      } else {
        SetDone(dust::Result<HttpResponse, HttpError>::Ok(std::move(response_)));
      }

      // Make sure the next PollOnce observes completion without relying on further IO events.
      io_ready_ = true;
      CancelAndTeardown();
      if (DebugHttp())
        std::fprintf(stderr, "[cpp-agent.http2] DONE: Wake (waker=%d)\n", waker_ ? 1 : 0);
      waker_.Wake();
      return;
    }
  }

  void SetDone(dust::Result<HttpResponse, HttpError> r) {
    if (done_)
      return;
    done_result_ = std::move(r);
    done_ = true;
  }

  dust::Poll<dust::Result<HttpResponse, HttpError>> TakeDone() {
    return dust::Poll<dust::Result<HttpResponse, HttpError>>::Ready(std::move(done_result_));
  }

  void CancelAndTeardown() {
    if (easy_ && multi_) {
      curl_multi_remove_handle(multi_, easy_);
    }

    if (easy_) {
      curl_easy_cleanup(easy_);
      easy_ = nullptr;
    }

    if (req_headers_) {
      curl_slist_free_all(req_headers_);
      req_headers_ = nullptr;
    }

    for (const auto& it : fd_actions_) {
      if (loop_)
        loop_->UnwatchFd(it.first);
    }
    fd_actions_.clear();

    done_ = true;
  }

  HttpRequest req_;
  HttpResponse response_;

  dust::MessageLoop* loop_ = nullptr;
  dust::WakerHandle waker_;

  bool alive_ = true;
  bool done_ = false;
  bool started_ = false;
  bool io_ready_ = false;

  dust::Result<HttpResponse, HttpError> done_result_ =
      dust::Result<HttpResponse, HttpError>::Err(
          HttpError{HttpErrorCode::kInvalidArgument, 0, "not started"});

  CURLM* multi_ = nullptr;
  CURL* easy_ = nullptr;
  curl_slist* req_headers_ = nullptr;

  std::unordered_map<int, int> fd_actions_;
  uint64_t timer_gen_ = 0;
};

static size_t WriteHeaderCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  const size_t n = size * nmemb;
  auto* self = reinterpret_cast<RequestFuture*>(userdata);

  std::string line(ptr, n);
  // header lines end with CRLF.
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
    line.pop_back();

  const auto pos = line.find(':');
  if (pos != std::string::npos) {
    std::string name = line.substr(0, pos);
    std::string value = line.substr(pos + 1);
    while (!value.empty() && value.front() == ' ')
      value.erase(value.begin());
    self->OnHeader(std::move(name), std::move(value));
  }

  return n;
}

static size_t WriteBodyCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  const size_t n = size * nmemb;
  auto* self = reinterpret_cast<RequestFuture*>(userdata);
  return self->OnBody(ptr, n);
}

static int SocketCb(CURL* /*easy*/, curl_socket_t s, int what, void* userp, void* /*socketp*/) {
  auto* self = reinterpret_cast<RequestFuture*>(userp);
  return self->OnSocket(static_cast<int>(s), what);
}

static int TimerCb(CURLM* /*multi*/, long timeout_ms, void* userp) {
  auto* self = reinterpret_cast<RequestFuture*>(userp);
  return self->OnTimer(timeout_ms);
}

}  // namespace

class HttpClient::Impl {
 public:
  Impl() = default;
};

HttpClient::HttpClient() : impl_(std::make_unique<Impl>()) {}
HttpClient::~HttpClient() = default;

dust::FuturePtr<dust::Result<HttpResponse, HttpError>> HttpClient::Send(HttpRequest req) {
  // Pseudo-code ("C++ + await"):
  // {
  //   // RequestFuture is the concrete poll-mode implementation.
  //   auto resp_or_err = await#1 RequestFuture(std::move(req));
  //   return std::move(resp_or_err);
  // }

  return dust::MakeRefPtr<RequestFuture>(std::move(req));
}

}  // namespace agent
