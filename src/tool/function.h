#pragma once

#include "nlohmann/json.hpp"

#include "dust/functional/closure.h"

#include <memory>
#include <string>

namespace agent {

struct FunctionSpec {
  // Must be globally unique, e.g. "file.read".
  std::string name;
  std::string description;

  // OpenAI native JSON schema for arguments, serialized as a JSON object string.
  std::string parameters_json;
};

class Function {
 public:
  virtual ~Function() = default;

  virtual const FunctionSpec& spec() const = 0;

  using OnDone = dust::OnceFunction<void(nlohmann::json out_result)>;
  using OnError = dust::OnceFunction<void(std::string error)>;

  // arguments: JSON object matching parameters schema.
  virtual void InvokeAsync(nlohmann::json arguments, OnDone on_done, OnError on_error) = 0;
};

using FunctionPtr = std::unique_ptr<Function>;

}  // namespace agent
