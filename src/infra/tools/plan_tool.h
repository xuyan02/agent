#pragma once

#include "infra/plan/plan_store.h"
#include "interfaces/itool.h"

#include <filesystem>
#include <memory>

namespace agent {

class PlanTool final : public agent::ITool {
public:
  explicit PlanTool(std::shared_ptr<agent::PlanStore> store);

  std::string Name() const override { return "plan"; }

  agent::ToolResult Invoke(const std::string& tool_call_id,
                                     const std::string& arguments_json,
                                     const agent::ToolContext& ctx) override;

private:
  std::shared_ptr<agent::PlanStore> store_;
};

} // namespace agent
