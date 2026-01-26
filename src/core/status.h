#pragma once

#include "core/errors.h"

#include <optional>
#include <string>
#include <utility>

namespace cpp_agent::core {

struct Status {
  ErrorCode code = ErrorCode::kInternal;
  std::string message;

  static Status Ok() { return {ErrorCode::kInternal, ""}; }
  static Status Error(ErrorCode c, std::string m) { return {c, std::move(m)}; }

  [[nodiscard]] bool ok() const noexcept { return message.empty(); }
};

template <typename T>
class Result {
public:
  Result(T v) : value_(std::move(v)) {}
  Result(Status s) : status_(std::move(s)) {}

  [[nodiscard]] bool ok() const noexcept { return value_.has_value(); }
  [[nodiscard]] const Status& status() const noexcept { return status_; }

  [[nodiscard]] T& value() & { return *value_; }
  [[nodiscard]] const T& value() const & { return *value_; }
  [[nodiscard]] T&& value() && { return std::move(*value_); }

private:
  std::optional<T> value_;
  Status status_;
};

} // namespace cpp_agent::core
