#include "runtime/runtime.h"

#include "runtime/team.h"

#include "interfaces/iconsole.h"

#include <cctype>
#include <iostream>

namespace agent {

static std::string Trim(std::string s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) b++;
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
  return s.substr(b, e - b);
}

Runtime::Runtime(agent::IConsole& console, std::unique_ptr<agent::LlmContext> llm)
    : console_(console), llm_(std::move(llm)) {
  if (!llm_) {
    std::cerr << "error: runtime created without llm\n";
  }
}

agent::LlmContext& Runtime::llm() { return *llm_; }

const agent::LlmContext& Runtime::llm() const { return *llm_; }

void Runtime::SetTeam(std::unique_ptr<Team> team) { team_ = std::move(team); }

void Runtime::OnCliLine(const std::string& line) {
  if (!team_) {
    std::cerr << "error: runtime has no team\n";
    return;
  }

  std::string target;
  std::string payload;

  if (!line.empty() && line[0] == '@') {
    const auto colon = line.find(':');
    if (colon != std::string::npos) {
      target = Trim(line.substr(1, colon - 1));
      payload = line.substr(colon + 1);
      if (!payload.empty() && payload.front() == ' ') payload.erase(0, 1);
    }
  }

  if (target.empty()) {
    target = team_->leader();
    payload = line;
  }

  DeliverToAgent(Message{.from = "master", .to = target, .content = payload});
}

void Runtime::Emit(const Message& msg) {
  if (msg.to == "master") {
    console_.Print(msg.content);
    return;
  }

  if (!team_) {
    std::cerr << "error: emit to agent without team: " << msg.to << "\n";
    return;
  }

  auto* agent = team_->Find(msg.to);
  if (!agent) {
    std::cerr << "error: unknown target: " << msg.to << "\n";
    return;
  }

  DeliverToAgent(msg);
}

void Runtime::DeliverToAgent(const Message& msg) {
  if (!team_) {
    std::cerr << "error: deliver without team\n";
    return;
  }

  auto* agent = team_->Find(msg.to);
  if (!agent) {
    std::cerr << "error: unknown agent: " << msg.to << "\n";
    return;
  }

  agent->Input(msg);
}

} // namespace agent
