#pragma once

#include "tool/tool_spec.h"

#include "dust/async/future.h"
#include "dust/memory/ref_counted.h"
#include "dust/memory/ref_ptr.h"

#include <nlohmann/json.hpp>

#include <string>

namespace agent {

class AgentContext;

// Tool is a named collection of functions.
class Tool : public dust::RefCounted {
 public:
  virtual ~Tool() = default;

  Tool(const Tool&) = delete;
  Tool& operator=(const Tool&) = delete;

  virtual const ToolSpec* GetSpec() const = 0;

  // Invokes a named function. Returns nullptr if function not found.
  virtual dust::FuturePtr<nlohmann::json> Invoke(dust::RefPtr<AgentContext> context,
                                                const std::string& function_name,
                                                const nlohmann::json& args) = 0;

 protected:
  Tool() = default;
};

}  // namespace agent
