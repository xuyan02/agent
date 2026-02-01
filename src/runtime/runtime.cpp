#include "runtime/runtime.h"

#include "agent/agent.h"
#include "agent/smart/smart_agent.h"

#include "interfaces/iconsole.h"

#include "json/json.h"
#include "llm/file_io.h"
#include "llm/llm_config_loader.h"
#include "llm/openai_provider_factory.h"

#include <cctype>


#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include <utility>

namespace agent {

std::filesystem::path Runtime::ComputeDefaultRootPath() {
  // Compute from the current user's home directory, without "$HOME" expansion.
  std::string home;
  if (const passwd* pw = ::getpwuid(::getuid())) {
    if (pw->pw_dir)
      home = pw->pw_dir;
  }

  if (home.empty())
    return std::filesystem::path(".agent");

  return std::filesystem::path(home) / ".agent";
}

namespace {

class SmartAgentFactory final : public agent::AgentFactory {
 public:
  const char* type() const override { return "smart"; }

  std::unique_ptr<agent::Agent> Create(agent::Runtime* runtime,
                                      std::string name,
                                      const nlohmann::json& /*params*/) const override {
    // Params are currently handled by SmartAgent itself (future extension).
    return std::make_unique<agent::SmartAgent>(runtime, std::move(name));
  }
};

}  // namespace

Runtime::Runtime(agent::IConsole& console, std::string root_path)
    : console_(console),
      llm_(std::make_unique<agent::LlmContext>()),
      root_path_(root_path.empty() ? ComputeDefaultRootPath() : std::filesystem::path(std::move(root_path))) {
  llm_->RegisterFactory(std::make_unique<agent::OpenAIProviderFactory>());
  RegisterAgentFactory(std::make_unique<SmartAgentFactory>());
}

void Runtime::RegisterAgentFactory(std::unique_ptr<agent::AgentFactory> factory) {
  agent_factories_.push_back(std::move(factory));
}

const agent::AgentFactory* Runtime::FindAgentFactory(const std::string& type) const {
  for (const auto& f : agent_factories_) {
    if (f && type == f->type())
      return f.get();
  }
  return nullptr;
}

std::string Runtime::GetPrompt(const std::string& name) const {
  if (name.empty())
    return {};

  // Very small input validation to avoid path traversal.
  for (char c : name) {
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'))
      return {};
  }

  // Built-in prompts live in the repository under src/prompts.
  // We resolve them relative to the runtime root path to keep the runtime layout consistent.
  const std::filesystem::path p = GetRootPath() / "src" / "prompts" / (name + ".md");
  std::string out;
  if (!agent::ReadAll(p, &out))
    return {};
  return out;
}

bool Runtime::Init() {
  if (!llm_) {
    console_.PrintLine("[cpp-agent.runtime] init: null llm");
    return false;
  }

  const std::filesystem::path cfg_path = GetRootPath() / "runtime.json";

  std::string json_text;
  if (!agent::ReadAll(cfg_path, &json_text)) {
    console_.PrintLine("[cpp-agent.runtime] init: failed to read runtime.json: " + cfg_path.string());
    return false;
  }

  auto root_opt = agent::json::Parse(json_text);
  if (!root_opt) {
    console_.PrintLine("[cpp-agent.runtime] init: failed to parse runtime.json");
    return false;
  }
  const auto& root = *root_opt;

  if (!root.is_object()) {
    console_.PrintLine("[cpp-agent.runtime] init: runtime.json must be an object");
    return false;
  }

  {
    auto it = root.find("providers");
    if (it == root.end() || !it->is_array()) {
      console_.PrintLine("[cpp-agent.runtime] init: runtime.json missing providers[]");
      return false;
    }

    if (!agent::RegisterProvidersFromConfig(*llm_, cfg_path)) {
      console_.PrintLine("[cpp-agent.runtime] init: RegisterProvidersFromConfig failed");
      console_.PrintLine("[cpp-agent.runtime] hint: check provider type/name/models and env expansion");
      return false;
    }
  }

  {
    auto it = root.find("agent");
    if (it == root.end() || !it->is_object()) {
      console_.PrintLine("[cpp-agent.runtime] init: runtime.json missing agent{}");
      return false;
    }

    auto type_opt = agent::json::GetString(*it, "type");
    auto name_opt = agent::json::GetString(*it, "name");

    if (!type_opt || !name_opt) {
      console_.PrintLine("[cpp-agent.runtime] init: agent must have type and name");
      return false;
    }

    const auto* factory = FindAgentFactory(*type_opt);
    if (!factory) {
      console_.PrintLine("[cpp-agent.runtime] init: unknown agent type: " + *type_opt);
      return false;
    }

    const auto& params = it->contains("params") ? (*it)["params"] : nlohmann::json::object();
    main_agent_ = factory->Create(this, *name_opt, params);
    if (!main_agent_) {
      console_.PrintLine("[cpp-agent.runtime] init: failed to create agent type=" + *type_opt + " name=" + *name_opt);
      return false;
    }
  }

  return true;
}

std::unique_ptr<agent::LlmRequest> Runtime::CreateRequest(
    std::string model_name,
    std::vector<agent::LlmMessage> messages,
    std::vector<agent::Tool*> tools,
    agent::LlmRequest::OnToken on_token,
    agent::LlmRequest::OnToolCalls on_tool_calls,
    agent::LlmRequest::OnDone on_done) {
  if (!llm_) {
    console_.PrintLine("[cpp-agent.runtime] create_request: null llm");
    return nullptr;
  }

  auto req = llm_->Create(std::move(model_name), std::move(messages), std::move(tools),
                          std::move(on_token), std::move(on_tool_calls), std::move(on_done));
  if (!req) {
    console_.PrintLine("[cpp-agent.runtime] create_request: failed (no provider supports model, or provider failed)");
    console_.PrintLine("[cpp-agent.runtime] hint: set CPP_AGENT_DEBUG_LLM_VERBOSE=1 to see model/provider selection");
  }
  return req;
}

void Runtime::RegisterTool(agent::ToolPtr t) {
  if (!t)
    return;

  t->Init();
  tools_[t->id] = std::move(t);
}

agent::Tool* Runtime::FindTool(const std::string& tool_id) const {
  auto it = tools_.find(tool_id);
  if (it == tools_.end() || !it->second)
    return nullptr;
  return it->second.get();
}

}  // namespace agent
