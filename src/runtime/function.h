#pragma once

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

  using OnDone = dust::OnceFunction<void(std::string out_result_json,
                                         std::string out_error)>;

  // arguments_json: JSON object string matching parameters schema.
  // done(out_result_json, out_error)
  // - out_result_json: JSON object string
  // - out_error: non-empty indicates failure
  virtual void InvokeAsync(std::string arguments_json, OnDone done) = 0;
};

using FunctionPtr = std::shared_ptr<Function>;

} // namespace agent
