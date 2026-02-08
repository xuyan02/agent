#include "tool/shell_tool.h"

#include "agent/agent_context.h"
#include "agent/session.h"
#include "json/json.h"
#include "tool/tool_spec.h"

#include "dust/async/just.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>

namespace agent {

namespace {

constexpr const char* kExecName = "shell.exec";

nlohmann::json Ok(nlohmann::json data) {
  nlohmann::json out;
  out["ok"] = true;
  out["data"] = std::move(data);
  return out;
}

nlohmann::json Err(std::string message) {
  nlohmann::json out;
  out["ok"] = false;
  out["error"] = std::move(message);
  return out;
}

}  // namespace

ShellTool::ShellTool() {
  ToolSpec::Builder tb;
  tb.SetName("shell").SetDescription("Execute shell commands in workspace");

  FunctionSpec::Builder fb;
  fb.SetName(kExecName)
      .SetDescription("Execute a shell command (bash -lc) with cwd=workspace");

  {
    FieldSpec::Builder c;
    c.SetName("command")
        .SetDescription("Shell command string")
        .SetRequired(true)
        .SetType(TypeSpecImplString::Builder().Build());
    fb.AddParam(std::move(c).Build());
  }

  tb.AddFunction(std::move(fb).Build());
  spec_ = std::make_unique<ToolSpec>(std::move(tb).Build());
}

ShellTool::~ShellTool() = default;

const ToolSpec* ShellTool::GetSpec() const {
  return spec_.get();
}

dust::FuturePtr<nlohmann::json> ShellTool::Invoke(dust::RefPtr<AgentContext> context,
                                                  const std::string& function_name,
                                                  const nlohmann::json& args) {
  if (function_name != kExecName)
    return nullptr;

  if (!context || !context->session())
    return dust::Just(Err("missing context/session"));

  auto cmd = json::GetString(args, "command");
  if (!cmd)
    return dust::Just(Err("missing 'command'"));

  const std::filesystem::path workspace = context->session()->workspace_path();

  // Run in workspace directory.
  std::error_code ec;
  std::filesystem::path old = std::filesystem::current_path(ec);
  std::filesystem::current_path(workspace, ec);
  if (ec)
    return dust::Just(Err("failed to chdir to workspace"));

  const std::string full = std::string("bash -lc ") + json::Dump(*cmd) + " 2>&1";
  FILE* pipe = ::popen(full.c_str(), "r");
  if (!pipe) {
    std::filesystem::current_path(old, ec);
    return dust::Just(Err("popen failed"));
  }

  std::string output;
  std::array<char, 4096> buf;
  while (true) {
    size_t n = std::fread(buf.data(), 1, buf.size(), pipe);
    if (n > 0)
      output.append(buf.data(), n);
    if (n < buf.size()) {
      if (std::feof(pipe))
        break;
      if (std::ferror(pipe))
        break;
    }
  }

  int rc = ::pclose(pipe);
  std::filesystem::current_path(old, ec);

  nlohmann::json data;
  data["exit_code"] = rc;
  data["output"] = output;
  return dust::Just(Ok(std::move(data)));
}

}  // namespace agent
