#pragma once

#include "tool/function.h"

#include "dust/functional/function.h"

#include <memory>
#include <string>
#include <vector>

namespace agent {

class Tool {
 public:
  virtual ~Tool() = default;

  // Activation unit.
  std::string id;
  std::string description;

  // Called once by the runtime before exposing this tool to the LLM.
  // Derived tools register functions here.
  virtual void Init() {}

  Function* FindFunction(const std::string& name) const;

  // Exposed for message codec / request building.
  const std::vector<FunctionPtr>& functions() const { return functions_; }

 private:
  using RegisteredFn = dust::Function<void(nlohmann::json arguments,
                                          agent::Function::OnDone on_done,
                                          agent::Function::OnError on_error)>;

 protected:
  // Register a single function implementation.
  // - name must be fully-qualified (e.g. "plan.add_task").
  // - description is shown to the model.
  // - parameters_json is an OpenAI JSON schema object as string.
 protected:
  void RegisterFunction(std::string name,
                        std::string description,
                        std::string parameters_json,
                        RegisteredFn fn);

 private:
  std::vector<FunctionPtr> functions_;
};

using ToolPtr = std::unique_ptr<Tool>;

}  // namespace agent
