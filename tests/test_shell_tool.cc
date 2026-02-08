#include "tool/shell_tool.h"

#include "agent/agent_context.h"
#include "agent/session.h"

#include "dust/memory/ref_ptr.h"

#include <cassert>
#include <filesystem>

#include <nlohmann/json.hpp>

int main() {
  const auto workspace = std::filesystem::temp_directory_path() / "cpp_agent_shell_tool";
  std::error_code ec;
  std::filesystem::remove_all(workspace, ec);
  std::filesystem::create_directories(workspace, ec);

  auto session = std::move(agent::Session::Builder()
                               .SetWorkspacePath(workspace)
                               .SetDefaultModel("fake"))
                     .Build();
  auto ctx = std::move(agent::AgentContext::Builder().SetSession(session)).Build();
  auto tool = dust::MakeRefPtr<agent::ShellTool>();

  nlohmann::json args;
  args["command"] = "pwd";

  auto fut = tool->Invoke(ctx, "shell.exec", args);
  assert(fut);

  dust::PollContext pc{dust::WakerHandle()};
  auto out = fut->PollOnce(pc).TakeReady();
  assert(out["ok"] == true);

  const std::string output = out["data"]["output"].get<std::string>();
  // pwd output includes trailing newline.
  assert(output.find(workspace.string()) != std::string::npos);

  return 0;
}
