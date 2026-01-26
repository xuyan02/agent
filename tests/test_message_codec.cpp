#include "runtime/message_codec.h"

#include <cassert>
#include <deque>

int main() {
  {
    std::deque<agent::Message> q;
    q.push_back(agent::Message{.from = "a", .to = "x", .content = "hello"});
    q.push_back(agent::Message{.from = "b", .to = "x", .content = "world"});

    const std::string s = agent::BuildAgentBatchInput(&q);
    assert(q.empty());
    assert(s == "@a: hello\n@b: world\n");
  }

  {
    const std::string text = "@u: hi\nline2\n@v: ok\n";
    auto msgs = agent::ParseAgentMultiTargetOutput("agent1", text);
    assert(msgs.size() == 2);
    assert(msgs[0].from == "agent1");
    assert(msgs[0].to == "u");
    assert(msgs[0].content == "hi\nline2");
    assert(msgs[1].to == "v");
    assert(msgs[1].content == "ok");
  }

  {
    // continuation without header is dropped
    const std::string text = "oops\n@u: hi\n";
    auto msgs = agent::ParseAgentMultiTargetOutput("a", text);
    assert(msgs.size() == 1);
    assert(msgs[0].to == "u");
  }

  return 0;
}
