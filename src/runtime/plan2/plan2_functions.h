#pragma once

#include "runtime/function.h"
#include "runtime/plan2/plan2_model.h"

namespace agent::plan2 {

class PlanAddTasksFunction final : public agent::Function {
public:
  explicit PlanAddTasksFunction(PlanModel* plan);
  const agent::FunctionSpec& spec() const override;
  bool Invoke(std::string arguments_json, std::string* out_result_json, std::string* out_error) override;

private:
  PlanModel* plan_;
  agent::FunctionSpec spec_;
};

class PlanSetStatusFunction final : public agent::Function {
public:
  explicit PlanSetStatusFunction(PlanModel* plan);
  const agent::FunctionSpec& spec() const override;
  bool Invoke(std::string arguments_json, std::string* out_result_json, std::string* out_error) override;

private:
  PlanModel* plan_;
  agent::FunctionSpec spec_;
};

class PlanRemoveTaskFunction final : public agent::Function {
public:
  explicit PlanRemoveTaskFunction(PlanModel* plan);
  const agent::FunctionSpec& spec() const override;
  bool Invoke(std::string arguments_json, std::string* out_result_json, std::string* out_error) override;

private:
  PlanModel* plan_;
  agent::FunctionSpec spec_;
};

} // namespace agent::plan2
