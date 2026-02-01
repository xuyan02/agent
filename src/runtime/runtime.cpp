#include "runtime/runtime.h"

#include "agent/agent.h"
#include "agent/smart/smart_agent.h"

#include "llm/llm_config_loader.h"
#include "llm/openai_provider_factory.h"
#include "llm/file_io.h"
#include "json/json.h"

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

Runtime::Runtime(std::string root_path)
    : llm_(std::make_unique<agent::LlmContext>()),
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

  const std::filesystem::path p = GetRootPath() / "prompts" / (name + ".md");
  std::string out;
  if (!agent::ReadAll(p, &out))
    return {};
  return out;
}

bool Runtime::Init() {
  if (!llm_)
    return false;

  const std::filesystem::path cfg_path = GetRootPath() / "runtime.json";

  std::string json_text;
  if (!agent::ReadAll(cfg_path, &json_text))
    return false;

  auto root_opt = agent::json::Parse(json_text);
  if (!root_opt)
    return false;
  const auto& root = *root_opt;

  if (!root.is_object())
    return false;

  {
    auto it = root.find("providers");
    if (it == root.end() || !it->is_array())
      return false;
    if (!agent::RegisterProvidersFromConfig(*llm_, cfg_path))
      return false;
  }

  {
    auto it = root.find("agent");
    if (it == root.end() || !it->is_object())
      return false;

    auto type_opt = agent::json::GetString(*it, "type");
    auto name_opt = agent::json::GetString(*it, "name");

    if (!type_opt || !name_opt)
      return false;

    const auto* factory = FindAgentFactory(*type_opt);
    if (!factory)
      return false;

    const auto& params = it->contains("params") ? (*it)["params"] : nlohmann::json::object();
    main_agent_ = factory->Create(this, *name_opt, params);
    if (!main_agent_)
      return false;
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
  if (!llm_)
    return nullptr;
  return llm_->Create(std::move(model_name), std::move(messages), std::move(tools),
                      std::move(on_token), std::move(on_tool_calls), std::move(on_done));
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
