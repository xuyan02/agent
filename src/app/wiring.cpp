#include "app/wiring.h"

#include "core/agent.h"
#include "core/policy.h"
#include "infra/console/cli_console.h"
#include "infra/llm/llm_config_loader.h"
#include "infra/llm/openai_provider_factory.h"
#include "infra/storage/json_file_storage.h"
#include "infra/tools/file_tools.h"
#include "infra/tools/plan_toolset.h"
#include "infra/tools/shell_tool.h"

#include <fstream>
#include <memory>
#include <sstream>
#include <unordered_map>

namespace cpp_agent::app {

cpp_agent::core::Result<std::unique_ptr<cpp_agent::core::Agent>> build_agent(
    const AppConfig& cfg, cpp_agent::interfaces::IConsole& console) {
  static cpp_agent::infra::storage::JsonFileStorage storage(cfg.storage_dir);
  // LLM providers (streaming via infra/llm).
  static cpp_agent::infra::llm::LlmContext llm;
  static bool llm_inited = false;
  if (!llm_inited) {
    llm_inited = true;
    llm.RegisterFactory(std::make_unique<cpp_agent::infra::llm::OpenAIProviderFactory>());
    (void)cpp_agent::infra::llm::RegisterProvidersFromConfig(llm, cfg.llm.providers_json_path);
  }

  cpp_agent::core::Policy policy(cfg.project_root);

  auto plan_path = cfg.storage_dir / "plan.json";
  auto plan_store = std::make_shared<cpp_agent::infra::plan::PlanStore>(plan_path);
  plan_store->load();

  std::unordered_map<std::string, std::unique_ptr<cpp_agent::interfaces::ITool>> tools;
  // Tool calling is disabled in streaming mode (for now).

  if (cfg.shell.enabled) {
    // no-op
  }

  cpp_agent::interfaces::LlmOptions opt;
  opt.model = cfg.llm.model;

  auto plan_prompt_path = cfg.project_root / cfg.plan_prompt_path;
  std::string plan_prompt_md;
  {
    std::ifstream ifs(plan_prompt_path);
    if (ifs) {
      std::ostringstream oss;
      oss << ifs.rdbuf();
      plan_prompt_md = oss.str();
    }
  }

  return std::make_unique<cpp_agent::core::Agent>(llm,
                                                  console,
                                                  storage,
                                                  std::move(policy),
                                                  std::move(tools),
                                                  opt,
                                                  *plan_store,
                                                  plan_prompt_md);
}

} // namespace cpp_agent::app
