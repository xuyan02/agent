#pragma once

#include "dust/async/future.h"
#include "dust/memory/ref_counted.h"

#include <string>

namespace agent {

class AgentContext;

class SystemPromptSegment : public dust::RefCounted {
 public:
  virtual ~SystemPromptSegment() = default;

  virtual dust::FuturePtr<std::string> Build(dust::RefPtr<AgentContext> context) = 0;

 protected:
  SystemPromptSegment() = default;
};

}  // namespace agent
