#pragma once

#include "runtime/function.h"
#include "runtime/plan2/plan2_model.h"

namespace agent::plan2 {

class PlanAddTaskFunction final : public agent::Function {
public:
  explicit PlanAddTaskFunction(PlanModel* plan);
  const agent::FunctionSpec& spec() const override;
  void InvokeAsync(std::string arguments_json, OnDone done) override;

private:
  PlanModel* plan_;
  agent::FunctionSpec spec_;
};

class PlanAddSubtaskFunction final : public agent::Function {
public:
  explicit PlanAddSubtaskFunction(PlanModel* plan);
  const agent::FunctionSpec& spec() const override;
  void InvokeAsync(std::string arguments_json, OnDone done) override;

private:
  PlanModel* plan_;
  agent::FunctionSpec spec_;
};

class PlanActivateTaskFunction final : public agent::Function {
public:
  explicit PlanActivateTaskFunction(PlanModel* plan);
  const agent::FunctionSpec& spec() const override;
  void InvokeAsync(std::string arguments_json, OnDone done) override;

private:
  PlanModel* plan_;
  agent::FunctionSpec spec_;
};

class PlanSuspendTaskFunction final : public agent::Function {
public:
  explicit PlanSuspendTaskFunction(PlanModel* plan);
  const agent::FunctionSpec& spec() const override;
  void InvokeAsync(std::string arguments_json, OnDone done) override;

private:
  PlanModel* plan_;
  agent::FunctionSpec spec_;
};

class PlanResumeTaskFunction final : public agent::Function {
public:
  explicit PlanResumeTaskFunction(PlanModel* plan);
  const agent::FunctionSpec& spec() const override;
  void InvokeAsync(std::string arguments_json, OnDone done) override;

private:
  PlanModel* plan_;
  agent::FunctionSpec spec_;
};

class PlanCompleteTaskFunction final : public agent::Function {
public:
  explicit PlanCompleteTaskFunction(PlanModel* plan);
  const agent::FunctionSpec& spec() const override;
  void InvokeAsync(std::string arguments_json, OnDone done) override;

private:
  PlanModel* plan_;
  agent::FunctionSpec spec_;
};

class PlanAbandonTaskFunction final : public agent::Function {
public:
  explicit PlanAbandonTaskFunction(PlanModel* plan);
  const agent::FunctionSpec& spec() const override;
  void InvokeAsync(std::string arguments_json, OnDone done) override;

private:
  PlanModel* plan_;
  agent::FunctionSpec spec_;
};

class PlanRemoveTaskFunction final : public agent::Function {
public:
  explicit PlanRemoveTaskFunction(PlanModel* plan);
  const agent::FunctionSpec& spec() const override;
  void InvokeAsync(std::string arguments_json, OnDone done) override;

private:
  PlanModel* plan_;
  agent::FunctionSpec spec_;
};

class PlanClearSubtasksFunction final : public agent::Function {
public:
  explicit PlanClearSubtasksFunction(PlanModel* plan);
  const agent::FunctionSpec& spec() const override;
  void InvokeAsync(std::string arguments_json, OnDone done) override;

private:
  PlanModel* plan_;
  agent::FunctionSpec spec_;
};

} // namespace agent::plan2
