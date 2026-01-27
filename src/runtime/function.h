#pragma once

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

  // arguments_json: JSON object string matching parameters schema.
  // out_result_json: JSON object string.
  virtual bool Invoke(std::string arguments_json,
                      std::string* out_result_json,
                      std::string* out_error) = 0;
};

using FunctionPtr = std::shared_ptr<Function>;

} // namespace agent
