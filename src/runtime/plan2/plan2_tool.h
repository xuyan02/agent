#pragma once

#include "interfaces/itool.h"
#include "runtime/plan2/plan2_model.h"

#include <memory>

namespace agent::plan2 {

class PlanTool final : public agent::ITool {
public:
  explicit PlanTool(PlanModel* plan);

  std::string Name() const override;

  agent::ToolResult Invoke(const std::string& tool_call_id,
                           const std::string& arguments_json,
                           const agent::ToolContext& ctx) override;

private:
  PlanModel* plan_;
};

} // namespace agent::plan2
