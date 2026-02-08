#pragma once

#include "dust/async/future.h"
#include "dust/async/result.h"

#include "http/http_types.h"

#include <memory>

namespace agent {

class HttpClient {
 public:
  HttpClient();
  ~HttpClient();

  HttpClient(const HttpClient&) = delete;
  HttpClient& operator=(const HttpClient&) = delete;

  dust::FuturePtr<dust::Result<HttpResponse, HttpError>> Send(HttpRequest req);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace agent
