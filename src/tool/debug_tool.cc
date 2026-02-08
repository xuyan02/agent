#include "tool/debug_tool.h"

#include "dust/async/just.h"
#include "tool/tool_spec.h"

#include <utility>

namespace agent {

namespace {

constexpr const char* kEchoName = "debug.echo";

}  // namespace

DebugTool::DebugTool() {
  FunctionSpec::Builder fb;
  fb.SetName(kEchoName).SetDescription("Echo input for debugging");

  {
    FieldSpec::Builder text_param;
    text_param.SetName("text")
        .SetDescription("Text to echo")
        .SetRequired(true)
        .SetType(TypeSpecImplString::Builder().Build());
    fb.AddParam(std::move(text_param).Build());
  }

  ToolSpec::Builder tb;
  tb.SetName("debug").SetDescription("Debug utilities").AddFunction(std::move(fb).Build());

  spec_ = std::make_unique<ToolSpec>(std::move(tb).Build());
}

DebugTool::~DebugTool() = default;

const ToolSpec* DebugTool::GetSpec() const {
  return spec_.get();
}

dust::FuturePtr<nlohmann::json> DebugTool::Invoke(dust::RefPtr<AgentContext>,
                                                 const std::string& function_name,
                                                 const nlohmann::json& args) {
  if (function_name != kEchoName)
    return nullptr;

  if (!args.is_object() || !args.contains("text") || !args["text"].is_string())
    return nullptr;

  return dust::Just(nlohmann::json(args["text"].get<std::string>()));
}

}  // namespace agent
