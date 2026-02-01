#include "tool/tool.h"

#include <utility>

namespace agent {

namespace {

class LambdaFunction final : public agent::Function {
 public:
  using Impl = dust::Function<void(nlohmann::json arguments,
                                  agent::Function::OnDone on_done,
                                  agent::Function::OnError on_error)>;

  LambdaFunction(agent::FunctionSpec spec, Impl fn)
      : spec_(std::move(spec)), fn_(std::move(fn)) {}

  const agent::FunctionSpec& spec() const override { return spec_; }

  void InvokeAsync(nlohmann::json arguments, OnDone on_done, OnError on_error) override {
    if (!fn_) {
      if (on_error)
        std::move(on_error)("null function");
      return;
    }
    fn_(std::move(arguments), std::move(on_done), std::move(on_error));
  }

 private:
  agent::FunctionSpec spec_;
  Impl fn_;
};

}  // namespace

void Tool::RegisterFunction(std::string name,
                            std::string description,
                            std::string parameters_json,
                            RegisteredFn fn) {
  agent::FunctionSpec spec;
  spec.name = std::move(name);
  spec.description = std::move(description);
  spec.parameters_json = std::move(parameters_json);

  functions_.push_back(std::make_unique<LambdaFunction>(std::move(spec), std::move(fn)));
}

Function* Tool::FindFunction(const std::string& name) const {
  for (const auto& f : functions_) {
    if (!f)
      continue;
    if (f->spec().name == name)
      return f.get();
  }
  return nullptr;
}

}  // namespace agent
