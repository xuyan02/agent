#pragma once

#include "agent/smart/smart_agent.h"
#include "tool/tool.h"

#include <string>

namespace agent {

class ShallowThinkTool final : public agent::Tool {
 public:
  explicit ShallowThinkTool(agent::SmartAgent* smart);

  void Init() override;

 private:
  void Think(nlohmann::json arguments,
             agent::Function::OnDone on_done,
             agent::Function::OnError on_error);

  agent::SmartAgent* smart_{nullptr};
};

}  // namespace agent
