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

namespace agent {

std::unique_ptr<agent::LlmContext> build_llm(const AppConfig& cfg) {
  auto llm = std::make_unique<agent::LlmContext>();
  llm->RegisterFactory(std::make_unique<agent::OpenAIProviderFactory>());

  if (!agent::RegisterProvidersFromConfig(*llm, cfg.llm.providers_json_path)) {
    return nullptr;
  }

  return llm;
}

std::unique_ptr<agent::Agent> build_agent(
    const AppConfig& cfg, agent::IConsole& console) {
  static agent::JsonFileStorage storage(cfg.storage_dir);

  static std::unique_ptr<agent::LlmContext> llm = build_llm(cfg);
  if (!llm) return nullptr;

  agent::Policy policy(cfg.project_root);

  auto plan_path = cfg.storage_dir / "plan.json";
  auto plan_store = std::make_shared<agent::PlanStore>(plan_path);
  plan_store->load();

  std::unordered_map<std::string, std::unique_ptr<agent::ITool>> tools;
  // Tool calling is disabled in streaming mode (for now).

  if (cfg.shell.enabled) {
    // no-op
  }

  agent::LlmOptions opt;
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

  return std::make_unique<agent::Agent>(*llm,
                                        console,
                                        storage,
                                        std::move(policy),
                                        std::move(tools),
                                        opt,
                                        *plan_store,
                                        plan_prompt_md);
}

} // namespace agent
