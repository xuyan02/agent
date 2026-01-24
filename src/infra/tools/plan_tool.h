#pragma once

#include "infra/plan/plan_store.h"
#include "interfaces/itool.h"

#include <filesystem>
#include <memory>

namespace cpp_agent::infra::tools {

class PlanTool final : public cpp_agent::interfaces::ITool {
public:
  explicit PlanTool(std::shared_ptr<cpp_agent::infra::plan::PlanStore> store);

  std::string name() const override { return "plan"; }

  cpp_agent::core::ToolResult invoke(const std::string& tool_call_id,
                                     const std::string& arguments_json,
                                     const cpp_agent::interfaces::ToolContext& ctx) override;

private:
  std::shared_ptr<cpp_agent::infra::plan::PlanStore> store_;
};

} // namespace cpp_agent::infra::tools
