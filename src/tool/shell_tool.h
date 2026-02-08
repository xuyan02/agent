#pragma once

#include "tool/tool.h"

#include <memory>

namespace agent {

class ShellTool final : public Tool {
 public:
  ShellTool();
  ~ShellTool() override;

  ShellTool(const ShellTool&) = delete;
  ShellTool& operator=(const ShellTool&) = delete;

  const ToolSpec* GetSpec() const override;

  dust::FuturePtr<nlohmann::json> Invoke(dust::RefPtr<AgentContext> context,
                                        const std::string& function_name,
                                        const nlohmann::json& args) override;

 private:
  std::unique_ptr<ToolSpec> spec_;
};

}  // namespace agent
