#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "dust/functional/function.h"

namespace agent {

struct HttpHeader {
  std::string name;
  std::string value;
};

struct HttpRequest {
  std::string method = "GET";
  std::string url;

  std::vector<HttpHeader> headers;
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

struct HttpResponse {
  long status = 0;
  std::vector<HttpHeader> headers;
  std::string body;
};

enum class HttpErrorCode {
  kOk = 0,
  kCancelled,
  kTimeout,
  kCurl,
  kInvalidArgument,
};

struct HttpError {
  HttpErrorCode code = HttpErrorCode::kOk;
  int curl_code = 0;
  std::string message;

  explicit operator bool() const { return code != HttpErrorCode::kOk; }
};

}  // namespace agent
