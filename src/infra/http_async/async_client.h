#ifndef INFRA_HTTP_ASYNC_ASYNC_CLIENT_H_
#define INFRA_HTTP_ASYNC_ASYNC_CLIENT_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "dust/functional/closure.h"

namespace dust {
class MessageLoop;
}

namespace http {

struct Header {
  std::string name;
  std::string value;
};

struct Request {
  std::string method = "GET";
  std::string url;

  std::vector<Header> headers;
  std::string body;

  // Total request timeout.
  std::optional<int64_t> timeout_ms;

  // Follow redirects.
  bool follow_redirects = true;

  // Maximum redirects to follow.
  int max_redirects = 5;

  // Streaming body callback. If set, it's invoked incrementally as data arrives.
  // Returning false aborts the transfer.
  using OnBodyChunk = dust::Function<bool(const char* data, size_t size)>;
  OnBodyChunk on_body_chunk;
};

struct Response {
  long status = 0;
  std::vector<Header> headers;
  std::string body;
};

enum class ErrorCode {
  kOk = 0,
  kCancelled,
  kTimeout,
  kCurl,
  kInvalidArgument,
};

struct Error {
  ErrorCode code = ErrorCode::kOk;
  int curl_code = 0;
  std::string message;

  explicit operator bool() const { return code != ErrorCode::kOk; }
};

struct Result {
  Response response;
  Error error;
};

using OnceCallback = dust::OnceFunction<void(Result)>;

class AsyncClient {
 public:
  // One-shot. Binds to the MessageLoop on the current thread and starts
  // immediately.
  AsyncClient(Request req, OnceCallback on_finish);
  ~AsyncClient();

  AsyncClient(const AsyncClient&) = delete;
  AsyncClient& operator=(const AsyncClient&) = delete;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace http

#endif  // INFRA_HTTP_ASYNC_ASYNC_CLIENT_H_
