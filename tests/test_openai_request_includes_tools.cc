#include <cassert>
#include <string>

// White-box test: ensure request JSON includes tools when tools_json is non-empty.
// We do this by including the implementation file (build_request_json is static).
#define CPP_AGENT_OPENAI_CLIENT_TEST
#include "../src/infra/http/openai_client.cc"

int main() {
  agent::LlmOptions opt;
  opt.model = "m";
  opt.temperature = 0.0;

  std::vector<agent::Message> msgs;
  agent::Message sys;
  sys.role = agent::Role::kSystem;
  sys.content = "s";
  msgs.push_back(sys);

  std::string tools = "[{\"type\":\"function\",\"function\":{\"name\":\"t\",\"parameters\":{\"type\":\"object\",\"properties\":{}}}}]";

  auto body = build_request_json(msgs, opt, tools);
  assert(body.find("\"tools\"") != std::string::npos);
  assert(body.find("\"tool_choice\":\"auto\"") != std::string::npos);
  return 0;
}
