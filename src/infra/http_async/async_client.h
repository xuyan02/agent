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

class Call {
 public:
  Call() = default;
  Call(Call&&) noexcept;
  Call& operator=(Call&&) noexcept;
  ~Call();

  Call(const Call&) = delete;
  Call& operator=(const Call&) = delete;

  void Cancel();
  bool valid() const;

 private:
  class Impl;
  friend class AsyncClient;

  explicit Call(std::shared_ptr<Impl> impl);
  std::shared_ptr<Impl> impl_;
};

class AsyncClient {
 public:
  // Binds to the MessageLoop on the current thread.
  AsyncClient();
  ~AsyncClient();

  AsyncClient(const AsyncClient&) = delete;
  AsyncClient& operator=(const AsyncClient&) = delete;

  using OnceCallback = dust::OnceFunction<void(Result)>;

  // Starts an async HTTP request.
  // The callback is invoked on the MessageLoop thread.
  Call Start(Request req, OnceCallback callback);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace http

#endif  // INFRA_HTTP_ASYNC_ASYNC_CLIENT_H_
