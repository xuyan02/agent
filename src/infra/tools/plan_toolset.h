#pragma once

#include "infra/plan/plan_store.h"
#include "interfaces/itool.h"

#include <memory>

namespace agent {

class PlanRenderTool final : public agent::ITool {
public:
  explicit PlanRenderTool(std::shared_ptr<agent::PlanStore> store);
  std::string Name() const override { return "plan.render"; }
  agent::ToolResult Invoke(const std::string& tool_call_id,
                                     const std::string& arguments_json,
                                     const agent::ToolContext& ctx) override;

private:
  std::shared_ptr<agent::PlanStore> store_;
};

class PlanAddTool final : public agent::ITool {
public:
  explicit PlanAddTool(std::shared_ptr<agent::PlanStore> store);
  std::string Name() const override { return "plan_add"; }
  agent::ToolResult Invoke(const std::string& tool_call_id,
                                     const std::string& arguments_json,
                                     const agent::ToolContext& ctx) override;

private:
  std::shared_ptr<agent::PlanStore> store_;
};

class PlanSwitchTool final : public agent::ITool {
public:
  explicit PlanSwitchTool(std::shared_ptr<agent::PlanStore> store);
  std::string Name() const override { return "plan_switch"; }
  agent::ToolResult Invoke(const std::string& tool_call_id,
                                     const std::string& arguments_json,
                                     const agent::ToolContext& ctx) override;

private:
  std::shared_ptr<agent::PlanStore> store_;
};

class PlanCompleteTool final : public agent::ITool {
public:
  explicit PlanCompleteTool(std::shared_ptr<agent::PlanStore> store);
  std::string Name() const override { return "plan_complete"; }
  agent::ToolResult Invoke(const std::string& tool_call_id,
                                     const std::string& arguments_json,
                                     const agent::ToolContext& ctx) override;

private:
  std::shared_ptr<agent::PlanStore> store_;
};

class PlanReplanTool final : public agent::ITool {
public:
  explicit PlanReplanTool(std::shared_ptr<agent::PlanStore> store);
  std::string Name() const override { return "plan_replan"; }
  agent::ToolResult Invoke(const std::string& tool_call_id,
                                     const std::string& arguments_json,
                                     const agent::ToolContext& ctx) override;

private:
  std::shared_ptr<agent::PlanStore> store_;
};

} // namespace agent
