#include "app/wiring.h"

#include "core/agent.h"
#include "core/policy.h"
#include "infra/console/cli_console.h"
#include "infra/http/openai_client.h"
#include "infra/storage/json_file_storage.h"
#include "infra/tools/file_tools.h"
#include "infra/tools/plan_toolset.h"
#include "infra/tools/shell_tool.h"

#include <memory>
#include <unordered_map>

namespace cpp_agent::app {

std::unique_ptr<cpp_agent::core::Agent> build_agent_or_throw(const AppConfig& cfg) {
  static cpp_agent::infra::console::CliConsole console;
  static cpp_agent::infra::storage::JsonFileStorage storage(cfg.storage_dir);
  static cpp_agent::infra::http::OpenAIClient llm(cfg.openai.base_url, cfg.openai.api_key);
  llm.set_log_requests(cfg.debug.log_llm);

  cpp_agent::core::Policy policy(cfg.project_root);

  auto plan_path = cfg.storage_dir / "plan.json";
  auto plan_store = std::make_shared<cpp_agent::infra::plan::PlanStore>(plan_path);
  plan_store->load();

  std::unordered_map<std::string, std::unique_ptr<cpp_agent::interfaces::ITool>> tools;
  tools.emplace("plan.render", std::make_unique<cpp_agent::infra::tools::PlanRenderTool>(plan_store));
  tools.emplace("plan.add", std::make_unique<cpp_agent::infra::tools::PlanAddTool>(plan_store));
  tools.emplace("plan.complete", std::make_unique<cpp_agent::infra::tools::PlanCompleteTool>(plan_store));
  tools.emplace("plan.active", std::make_unique<cpp_agent::infra::tools::PlanActiveTool>(plan_store));
  tools.emplace("plan.replan", std::make_unique<cpp_agent::infra::tools::PlanReplanTool>(plan_store));
  tools.emplace("read_file", std::make_unique<cpp_agent::infra::tools::ReadFileTool>());
  tools.emplace("write_file", std::make_unique<cpp_agent::infra::tools::WriteFileTool>());

  llm.set_tools_json("["
                    "{\"type\":\"function\",\"function\":{\"name\":\"plan.render\",\"description\":\"Render current plan markdown\",\"parameters\":{\"type\":\"object\",\"properties\":{}}}},"
                    "{\"type\":\"function\",\"function\":{\"name\":\"plan.add\",\"description\":\"Add a task. If parent_no omitted => root task.\",\"parameters\":{\"type\":\"object\",\"properties\":{\"parent_no\":{\"type\":\"string\"},\"after_no\":{\"type\":\"string\"},\"goal\":{\"type\":\"string\"},\"title\":{\"type\":\"string\"}},\"required\":[\"goal\",\"title\"]}}},"
                    "{\"type\":\"function\",\"function\":{\"name\":\"plan.active\",\"description\":\"Set active leaf task by no\",\"parameters\":{\"type\":\"object\",\"properties\":{\"no\":{\"type\":\"string\"}},\"required\":[\"no\"]}}},"
                    "{\"type\":\"function\",\"function\":{\"name\":\"plan.complete\",\"description\":\"Complete (delete) a task by no\",\"parameters\":{\"type\":\"object\",\"properties\":{\"no\":{\"type\":\"string\"}},\"required\":[\"no\"]}}},"
                    "{\"type\":\"function\",\"function\":{\"name\":\"plan.replan\",\"description\":\"Replace children list. history_line required.\",\"parameters\":{\"type\":\"object\",\"properties\":{\"no\":{\"type\":\"string\"},\"history_line\":{\"type\":\"string\"},\"new_children\":{\"type\":\"array\",\"items\":{\"type\":\"object\",\"properties\":{\"goal\":{\"type\":\"string\"},\"title\":{\"type\":\"string\"},\"children\":{\"type\":\"array\",\"items\":{\"type\":\"object\"}}},\"required\":[\"goal\",\"title\"]}}},\"required\":[\"no\",\"history_line\",\"new_children\"]}}}},"
                    "{\"type\":\"function\",\"function\":{\"name\":\"read_file\",\"description\":\"Read a text file\",\"parameters\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}}},"
                    "{\"type\":\"function\",\"function\":{\"name\":\"write_file\",\"description\":\"Write a text file\",\"parameters\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},\"required\":[\"path\",\"content\"]}}}"
                    "]");

  if (cfg.shell.enabled) {
    tools.emplace("run_shell_command", std::make_unique<cpp_agent::infra::tools::ShellTool>(cfg.shell.timeout_ms));
    llm.set_tools_json("["
                      "{\"type\":\"function\",\"function\":{\"name\":\"plan.render\",\"description\":\"Render current plan markdown\",\"parameters\":{\"type\":\"object\",\"properties\":{}}}},"
                      "{\"type\":\"function\",\"function\":{\"name\":\"plan.add\",\"description\":\"Add a task. If parent_no omitted => root task.\",\"parameters\":{\"type\":\"object\",\"properties\":{\"parent_no\":{\"type\":\"string\"},\"after_no\":{\"type\":\"string\"},\"goal\":{\"type\":\"string\"},\"title\":{\"type\":\"string\"}},\"required\":[\"goal\",\"title\"]}}},"
                      "{\"type\":\"function\",\"function\":{\"name\":\"plan.active\",\"description\":\"Set active leaf task by no\",\"parameters\":{\"type\":\"object\",\"properties\":{\"no\":{\"type\":\"string\"}},\"required\":[\"no\"]}}},"
                      "{\"type\":\"function\",\"function\":{\"name\":\"plan.complete\",\"description\":\"Complete (delete) a task by no\",\"parameters\":{\"type\":\"object\",\"properties\":{\"no\":{\"type\":\"string\"}},\"required\":[\"no\"]}}},"
                      "{\"type\":\"function\",\"function\":{\"name\":\"plan.replan\",\"description\":\"Replace children list. history_line required.\",\"parameters\":{\"type\":\"object\",\"properties\":{\"no\":{\"type\":\"string\"},\"history_line\":{\"type\":\"string\"},\"new_children\":{\"type\":\"array\",\"items\":{\"type\":\"object\",\"properties\":{\"goal\":{\"type\":\"string\"},\"title\":{\"type\":\"string\"},\"children\":{\"type\":\"array\",\"items\":{\"type\":\"object\"}}},\"required\":[\"goal\",\"title\"]}}},\"required\":[\"no\",\"history_line\",\"new_children\"]}}},"
                      "{\"type\":\"function\",\"function\":{\"name\":\"read_file\",\"description\":\"Read a text file\",\"parameters\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}}},"
                      "{\"type\":\"function\",\"function\":{\"name\":\"write_file\",\"description\":\"Write a text file\",\"parameters\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}},\"required\":[\"path\",\"content\"]}}},"
                      "{\"type\":\"function\",\"function\":{\"name\":\"run_shell_command\",\"description\":\"Run a shell command\",\"parameters\":{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\"}},\"required\":[\"command\"]}}}"
                      "]");
  }

  cpp_agent::interfaces::LlmOptions opt;
  opt.model = cfg.openai.model;

  return std::make_unique<cpp_agent::core::Agent>(llm, console, storage, std::move(policy), std::move(tools), opt);
}

} // namespace cpp_agent::app
