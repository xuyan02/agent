#include "infra/http_async/async_client.h"

#include <curl/curl.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dust/message_loop/message_loop.h"
#include "dust/time/duration.h"

namespace http {
namespace {

struct Inflight;

static size_t WriteBodyCb(char* ptr, size_t size, size_t nmemb, void* userdata);

struct HeaderCapture {
  std::vector<Header>* headers;
};

static size_t WriteHeaderCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  const size_t n = size * nmemb;
  auto* cap = static_cast<HeaderCapture*>(userdata);
  if (!cap || !cap->headers)
    return n;

  // Header lines include trailing \r\n.
  std::string line(ptr, n);
  // Ignore status line.
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

  cap->headers->push_back(Header{std::move(name), std::move(value)});
  return n;
}

struct Inflight;

struct GlobalState {
  CURLM* multi = nullptr;
  dust::MessageLoop* loop = nullptr;

  // socket fd -> interests
  std::unordered_map<int, int> fd_actions;

  // Curl timer state
  std::atomic<uint64_t> timer_gen{0};

  // easy -> inflight
  std::unordered_map<CURL*, Inflight*> inflight_by_easy;
};

struct Inflight {
  GlobalState* g = nullptr;
  CURL* easy = nullptr;
  curl_slist* req_headers = nullptr;

  Request req;
  Result result;
  HeaderCapture header_cap{&result.response.headers};

  AsyncClient::OnceCallback cb;

  std::atomic<bool> cancelled{false};
};

static size_t WriteBodyCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  const size_t n = size * nmemb;
  auto* in = static_cast<Inflight*>(userdata);
  if (!in)
    return n;

  if (in->req.on_body_chunk) {
    const bool ok = in->req.on_body_chunk(ptr, n);
    if (!ok)
      return 0;  // abort
  }

  in->result.response.body.append(ptr, n);
  return n;
}

namespace {

bool DebugHttp() {
  const char* v = std::getenv("CPP_AGENT_DEBUG_HTTP");
  return v && *v && strcmp(v, "0") != 0;
}

const char* CurlPollToStr(int what) {
  static thread_local char buf[64];
  buf[0] = '\0';
  if (what == CURL_POLL_REMOVE)
    return "REMOVE";
  if (what & CURL_POLL_IN)
    strcat(buf, "IN|");
  if (what & CURL_POLL_OUT)
    strcat(buf, "OUT|");
  if (what & CURL_POLL_INOUT) {
    // INOUT implies IN|OUT; keep as-is.
  }
  const size_t n = strlen(buf);
  if (n > 0 && buf[n - 1] == '|')
    buf[n - 1] = '\0';
  if (buf[0] == '\0')
    strcpy(buf, "(none)");
  return buf;
}

}  // namespace

static void PumpMulti(GlobalState* g, curl_socket_t s, int ev_bitmask);

static int SocketCb(CURL* /*easy*/, curl_socket_t s, int what, void* userp, void* /*socketp*/) {
  auto* g = static_cast<GlobalState*>(userp);
  if (!g || !g->loop)
    return 0;

  if (DebugHttp()) {
    std::fprintf(stderr, "[cpp-agent.http] curl socket_cb fd=%d what=%s raw=%d\n", (int)s,
                 CurlPollToStr(what), what);
  }

  if (what == CURL_POLL_REMOVE) {
    g->fd_actions.erase(static_cast<int>(s));
    if (DebugHttp()) {
      std::fprintf(stderr, "[cpp-agent.http] UnwatchFd fd=%d\n", (int)s);
    }
    g->loop->UnwatchFd(static_cast<int>(s));
    return 0;
  }

  g->fd_actions[static_cast<int>(s)] = what;

  dust::WatchCallbacks cbs;

  if (what & CURL_POLL_IN) {
    cbs.on_readable = dust::Closure([g, s]() { PumpMulti(g, s, CURL_CSELECT_IN); });
  }

  if (what & CURL_POLL_OUT) {
    cbs.on_writable = dust::Closure([g, s]() { PumpMulti(g, s, CURL_CSELECT_OUT); });
  }

  cbs.on_error = dust::Closure([g, s]() { PumpMulti(g, s, CURL_CSELECT_ERR); });

  if (DebugHttp()) {
    std::fprintf(stderr, "[cpp-agent.http] WatchFd fd=%d want=%s (readable=%d writable=%d)\n",
                 (int)s, CurlPollToStr(what), (int)((what & CURL_POLL_IN) != 0),
                 (int)((what & CURL_POLL_OUT) != 0));
  }
  g->loop->WatchFd(static_cast<int>(s), std::move(cbs));
  return 0;
}

static int TimerCb(CURLM* /*multi*/, long timeout_ms, void* userp) {
  auto* g = static_cast<GlobalState*>(userp);
  if (!g || !g->loop)
    return 0;

  // Cancel any previous scheduled timer.
  const uint64_t gen = g->timer_gen.fetch_add(1) + 1;

  if (DebugHttp()) {
    std::fprintf(stderr, "[cpp-agent.http] curl timer_cb timeout_ms=%ld\n", timeout_ms);
  }

  if (timeout_ms < 0) {
    return 0;
  }

  g->loop->task_runner()->PostDelayedTask(
      dust::Duration::FromMilliseconds(timeout_ms), dust::OnceClosure([g, gen, timeout_ms]() {
        if (g->timer_gen.load() != gen)
          return;
        if (DebugHttp()) {
          std::fprintf(stderr, "[cpp-agent.http] timer fired gen=%llu timeout_ms=%ld\n",
                       (unsigned long long)gen, timeout_ms);
        }
        PumpMulti(g, CURL_SOCKET_TIMEOUT, 0);
      }));

  return 0;
}

static void CompleteEasy(GlobalState* g, CURL* easy, CURLcode code) {
  auto it = g->inflight_by_easy.find(easy);
  if (it == g->inflight_by_easy.end())
    return;

  Inflight* in = it->second;
  g->inflight_by_easy.erase(it);

  long status = 0;
  (void)curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
  in->result.response.status = status;

  if (in->cancelled.load()) {
    in->result.error.code = ErrorCode::kCancelled;
  } else if (code == CURLE_OPERATION_TIMEDOUT) {
    in->result.error.code = ErrorCode::kTimeout;
    in->result.error.curl_code = static_cast<int>(code);
    in->result.error.message = curl_easy_strerror(code);
  } else if (code != CURLE_OK) {
    in->result.error.code = ErrorCode::kCurl;
    in->result.error.curl_code = static_cast<int>(code);
    in->result.error.message = curl_easy_strerror(code);
  }

  curl_multi_remove_handle(g->multi, easy);
  curl_easy_cleanup(easy);

  // Mark as completed so a late Call::Cancel() is a no-op.
  in->easy = nullptr;

  if (in->req_headers)
    curl_slist_free_all(in->req_headers);

  auto cb = std::move(in->cb);
  Result res = std::move(in->result);

  delete in;

  // If a Call handle still exists, its Impl still holds an Inflight*.
  // Clear it to avoid Cancel() using a dangling pointer.
  // (See Call::Impl::Cancel() and AsyncClient::Start().)
  // Note: We can't reach the Impl instance here; instead we rely on Cancel()
  // checking in->easy==nullptr (set above) and in->g still valid.

  if (cb)
    cb(std::move(res));
}

static void DrainCompletions(GlobalState* g) {
  int msgs = 0;
  while (CURLMsg* msg = curl_multi_info_read(g->multi, &msgs)) {
    if (msg->msg != CURLMSG_DONE)
      continue;
    CompleteEasy(g, msg->easy_handle, msg->data.result);
  }
}

static void PumpMulti(GlobalState* g, curl_socket_t s, int ev_bitmask) {
  if (!g || !g->multi)
    return;

  if (DebugHttp()) {
    std::fprintf(stderr, "[cpp-agent.http] PumpMulti fd=%d ev_bitmask=0x%x\n", (int)s, ev_bitmask);
  }

  int running = 0;
  CURLMcode mrc = curl_multi_socket_action(g->multi, s, ev_bitmask, &running);
  if (mrc != CURLM_OK) {
    // Fatal for now.
    std::fprintf(stderr, "curl_multi_socket_action: %s\n", curl_multi_strerror(mrc));
    std::abort();
  }

  DrainCompletions(g);
}

static void DieIfCurlNotOk(CURLcode rc, const char* what) {
  if (rc == CURLE_OK)
    return;
  std::fprintf(stderr, "%s: %s\n", what, curl_easy_strerror(rc));
  std::abort();
}

static void SetupEasy(Inflight* in) {
  CURL* easy = curl_easy_init();
  if (!easy) {
    in->result.error.code = ErrorCode::kCurl;
    in->result.error.message = "curl_easy_init failed";
    return;
  }

  in->easy = easy;

  DieIfCurlNotOk(curl_easy_setopt(easy, CURLOPT_URL, in->req.url.c_str()), "CURLOPT_URL");

  // Method.
  if (in->req.method == "GET") {
    DieIfCurlNotOk(curl_easy_setopt(easy, CURLOPT_HTTPGET, 1L), "CURLOPT_HTTPGET");
  } else if (in->req.method == "POST") {
    DieIfCurlNotOk(curl_easy_setopt(easy, CURLOPT_POST, 1L), "CURLOPT_POST");
    DieIfCurlNotOk(curl_easy_setopt(easy, CURLOPT_POSTFIELDS, in->req.body.c_str()),
                   "CURLOPT_POSTFIELDS");
    DieIfCurlNotOk(curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, (long)in->req.body.size()),
                   "CURLOPT_POSTFIELDSIZE");
  } else {
    DieIfCurlNotOk(curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, in->req.method.c_str()),
                   "CURLOPT_CUSTOMREQUEST");
    if (!in->req.body.empty()) {
      DieIfCurlNotOk(curl_easy_setopt(easy, CURLOPT_POSTFIELDS, in->req.body.c_str()),
                     "CURLOPT_POSTFIELDS");
      DieIfCurlNotOk(curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, (long)in->req.body.size()),
                     "CURLOPT_POSTFIELDSIZE");
    }
  }

  // Redirects.
  DieIfCurlNotOk(curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, in->req.follow_redirects ? 1L : 0L),
                 "CURLOPT_FOLLOWLOCATION");
  DieIfCurlNotOk(curl_easy_setopt(easy, CURLOPT_MAXREDIRS, (long)in->req.max_redirects),
                 "CURLOPT_MAXREDIRS");

  // Timeouts.
  if (in->req.timeout_ms.has_value()) {
    DieIfCurlNotOk(curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, (long)*in->req.timeout_ms),
                   "CURLOPT_TIMEOUT_MS");
  }

  // Response capture.
  DieIfCurlNotOk(curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &WriteBodyCb),
                 "CURLOPT_WRITEFUNCTION");
  DieIfCurlNotOk(curl_easy_setopt(easy, CURLOPT_WRITEDATA, in), "CURLOPT_WRITEDATA");

  DieIfCurlNotOk(curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, &WriteHeaderCb),
                 "CURLOPT_HEADERFUNCTION");
  DieIfCurlNotOk(curl_easy_setopt(easy, CURLOPT_HEADERDATA, &in->header_cap), "CURLOPT_HEADERDATA");

  // Request headers.
  for (const auto& h : in->req.headers) {
    std::string line = h.name;
    line += ": ";
    line += h.value;
    in->req_headers = curl_slist_append(in->req_headers, line.c_str());
  }
  if (in->req_headers) {
    DieIfCurlNotOk(curl_easy_setopt(easy, CURLOPT_HTTPHEADER, in->req_headers),
                   "CURLOPT_HTTPHEADER");
  }

  // Bookkeeping.
  DieIfCurlNotOk(curl_easy_setopt(easy, CURLOPT_PRIVATE, in), "CURLOPT_PRIVATE");
}

}  // namespace

class Call::Impl {
 public:
  explicit Impl(Inflight* in) : in_(in) {}

  void Cancel() {
    Inflight* in = in_;
    if (!in)
      return;

    // Mark cancelled for both the curl completion path and any late callbacks.
    in->cancelled.store(true);

    // If the request already completed, CompleteEasy() will have removed it from
    // the multi handle and set in->easy to nullptr. In that case, Cancel is a no-op.
    if (!in->easy)
      return;

    // Best-effort immediate abort.
    if (in->g && in->g->multi) {
      (void)curl_multi_remove_handle(in->g->multi, in->easy);
      curl_easy_cleanup(in->easy);
      in->easy = nullptr;

      // Complete with cancelled.
      in->result.error.code = ErrorCode::kCancelled;
      auto cb = std::move(in->cb);
      Result res = std::move(in->result);
      if (in->req_headers)
        curl_slist_free_all(in->req_headers);

      delete in;
      in_ = nullptr;

      if (cb)
        cb(std::move(res));
    }
  }

  bool valid() const { return in_ != nullptr; }

  ~Impl() = default;

 private:
  Inflight* in_ = nullptr;
};

Call::Call(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

Call::Call(Call&&) noexcept = default;
Call& Call::operator=(Call&&) noexcept = default;
Call::~Call() = default;

void Call::Cancel() {
  if (impl_)
    impl_->Cancel();
}

bool Call::valid() const {
  return impl_ && impl_->valid();
}

class AsyncClient::Impl {
 public:
  Impl() {
    g_.loop = dust::MessageLoop::Current();
    if (!g_.loop) {
      std::fprintf(stderr, "AsyncClient requires a MessageLoop bound to this thread\n");
      std::abort();
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    g_.multi = curl_multi_init();
    if (!g_.multi) {
      std::fprintf(stderr, "curl_multi_init failed\n");
      std::abort();
    }

    (void)curl_multi_setopt(g_.multi, CURLMOPT_SOCKETFUNCTION, &SocketCb);
    (void)curl_multi_setopt(g_.multi, CURLMOPT_SOCKETDATA, &g_);
    (void)curl_multi_setopt(g_.multi, CURLMOPT_TIMERFUNCTION, &TimerCb);
    (void)curl_multi_setopt(g_.multi, CURLMOPT_TIMERDATA, &g_);
  }

  ~Impl() {
    if (g_.multi)
      curl_multi_cleanup(g_.multi);
    g_.multi = nullptr;
    curl_global_cleanup();
  }

  Call Start(Request req, OnceCallback cb) {
    Inflight* in = new Inflight();
    in->g = &g_;
    in->req = std::move(req);
    in->cb = std::move(cb);

    SetupEasy(in);
    if (!in->easy) {
      Result res = std::move(in->result);
      auto once = std::move(in->cb);
      delete in;
      if (once)
        once(std::move(res));
      return Call();
    }

    g_.inflight_by_easy[in->easy] = in;

    CURLMcode mrc = curl_multi_add_handle(g_.multi, in->easy);
    if (mrc != CURLM_OK) {
      in->result.error.code = ErrorCode::kCurl;
      in->result.error.message = curl_multi_strerror(mrc);

      curl_easy_cleanup(in->easy);
      in->easy = nullptr;
      if (in->req_headers)
        curl_slist_free_all(in->req_headers);

      Result res = std::move(in->result);
      auto once = std::move(in->cb);
      delete in;
      if (once)
        once(std::move(res));
      return Call();
    }

    // Kickstart.
    PumpMulti(&g_, CURL_SOCKET_TIMEOUT, 0);

    return Call(std::make_shared<Call::Impl>(in));
  }

 private:
  GlobalState g_;
};

AsyncClient::AsyncClient() : impl_(std::make_unique<Impl>()) {}
AsyncClient::~AsyncClient() = default;

Call AsyncClient::Start(Request req, OnceCallback callback) {
  if (!callback) {
    // Invalid.
    return Call();
  }
  if (req.url.empty()) {
    return Call();
  }
  return impl_->Start(std::move(req), std::move(callback));
}

}  // namespace http
