#pragma once

#include "tool/tool.h"

#include "dust/memory/ref_ptr.h"

#include <memory>

namespace agent {

class DebugTool final : public Tool {
 public:
  DebugTool();
  ~DebugTool() override;

  DebugTool(const DebugTool&) = delete;
  DebugTool& operator=(const DebugTool&) = delete;

  const ToolSpec* GetSpec() const override;

  dust::FuturePtr<nlohmann::json> Invoke(dust::RefPtr<AgentContext> context,
                                        const std::string& function_name,
                                        const nlohmann::json& args) override;

 private:
  std::unique_ptr<ToolSpec> spec_;
};

}  // namespace agent
