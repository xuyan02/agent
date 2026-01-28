#include "infra/llm/openai_stream_accumulator.h"

#include <cassert>
#include <iostream>
#include <string>

static void test_extracts_tool_calls_from_stream() {
  agent::OpenAIStreamAccumulator acc;

  {
    const std::string line = R"({"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_1","type":"function","function":{"name":"foo","arguments":"{\"x\":"}}]}},"finish_reason":null}]})";
    agent::OpenAIStreamDelta delta;
    assert(acc.FeedDataLine(line, &delta));
    assert(delta.tool_calls_delta.size() == 1u);
    assert(delta.tool_calls_delta[0].id == "call_1");
    assert(delta.tool_calls_delta[0].name == "foo");
    assert(delta.tool_calls_delta[0].arguments_json == "{\\\"x\\\":");
  }

  {
    const std::string line = R"({"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"1}"}}]},"finish_reason":null}]})";
    agent::OpenAIStreamDelta delta;
    assert(acc.FeedDataLine(line, &delta));
    assert(delta.tool_calls_delta.size() == 1u);
    assert(delta.tool_calls_delta[0].arguments_json == "1}");
  }

  assert(acc.HasToolCalls());
  auto m = acc.BuildAssistantMessage();
  assert(m.tool_calls.size() == 1u);
  assert(m.tool_calls[0].id == "call_1");
  assert(m.tool_calls[0].name == "foo");
  assert(m.tool_calls[0].arguments_json == "{\\\"x\\\":1}");
}

int main() {
  test_extracts_tool_calls_from_stream();
  std::cout << "ok\n";
  return 0;
}
