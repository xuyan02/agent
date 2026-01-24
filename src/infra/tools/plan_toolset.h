#pragma once

#include "infra/plan/plan_store.h"
#include "interfaces/itool.h"

#include <memory>

namespace cpp_agent::infra::tools {

class PlanRenderTool final : public cpp_agent::interfaces::ITool {
public:
  explicit PlanRenderTool(std::shared_ptr<cpp_agent::infra::plan::PlanStore> store);
  std::string name() const override { return "plan.render"; }
  cpp_agent::core::ToolResult invoke(const std::string& tool_call_id,
                                     const std::string& arguments_json,
                                     const cpp_agent::interfaces::ToolContext& ctx) override;

private:
  std::shared_ptr<cpp_agent::infra::plan::PlanStore> store_;
};

class PlanAddTool final : public cpp_agent::interfaces::ITool {
public:
  explicit PlanAddTool(std::shared_ptr<cpp_agent::infra::plan::PlanStore> store);
  std::string name() const override { return "plan.add"; }
  cpp_agent::core::ToolResult invoke(const std::string& tool_call_id,
                                     const std::string& arguments_json,
                                     const cpp_agent::interfaces::ToolContext& ctx) override;

private:
  std::shared_ptr<cpp_agent::infra::plan::PlanStore> store_;
};

class PlanActiveTool final : public cpp_agent::interfaces::ITool {
public:
  explicit PlanActiveTool(std::shared_ptr<cpp_agent::infra::plan::PlanStore> store);
  std::string name() const override { return "plan.active"; }
  cpp_agent::core::ToolResult invoke(const std::string& tool_call_id,
                                     const std::string& arguments_json,
                                     const cpp_agent::interfaces::ToolContext& ctx) override;

private:
  std::shared_ptr<cpp_agent::infra::plan::PlanStore> store_;
};

class PlanCompleteTool final : public cpp_agent::interfaces::ITool {
public:
  explicit PlanCompleteTool(std::shared_ptr<cpp_agent::infra::plan::PlanStore> store);
  std::string name() const override { return "plan.complete"; }
  cpp_agent::core::ToolResult invoke(const std::string& tool_call_id,
                                     const std::string& arguments_json,
                                     const cpp_agent::interfaces::ToolContext& ctx) override;

private:
  std::shared_ptr<cpp_agent::infra::plan::PlanStore> store_;
};

class PlanReplanTool final : public cpp_agent::interfaces::ITool {
public:
  explicit PlanReplanTool(std::shared_ptr<cpp_agent::infra::plan::PlanStore> store);
  std::string name() const override { return "plan.replan"; }
  cpp_agent::core::ToolResult invoke(const std::string& tool_call_id,
                                     const std::string& arguments_json,
                                     const cpp_agent::interfaces::ToolContext& ctx) override;

private:
  std::shared_ptr<cpp_agent::infra::plan::PlanStore> store_;
};

} // namespace cpp_agent::infra::tools
