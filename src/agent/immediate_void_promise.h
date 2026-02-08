#pragma once

#include "dust/async/future.h"

#include <string>

namespace agent {

class ImmediateVoidPromise final : public dust::Future<dust::Result<void, std::string>> {
 public:
  explicit ImmediateVoidPromise(dust::Result<void, std::string> result)
      : result_(std::move(result)) {}

  dust::Poll<dust::Result<void, std::string>> PollOnce(dust::PollContext&) override {
    if (done_)
      return dust::Poll<dust::Result<void, std::string>>::Pending();
    done_ = true;
    return dust::Poll<dust::Result<void, std::string>>::Ready(std::move(result_));
  }

 private:
  dust::Result<void, std::string> result_;
  bool done_ = false;
};

}  // namespace agent
