#pragma once

#include "tool/tool.h"

#include "dust/memory/ref_ptr.h"

#include <memory>

namespace agent {

class FileTool final : public Tool {
 public:
  FileTool();
  ~FileTool() override;

  FileTool(const FileTool&) = delete;
  FileTool& operator=(const FileTool&) = delete;

  const ToolSpec* GetSpec() const override;

  dust::FuturePtr<nlohmann::json> Invoke(dust::RefPtr<AgentContext> context,
                                        const std::string& function_name,
                                        const nlohmann::json& args) override;

 private:
  std::unique_ptr<ToolSpec> spec_;
};

}  // namespace agent
