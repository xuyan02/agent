#include "runtime/message_codec.h"

#include <cctype>

namespace agent {

static std::string Trim(std::string s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) b++;
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
  return s.substr(b, e - b);
}

std::string BuildAgentBatchInput(std::deque<Message>* q) {
  std::string out;
  while (q && !q->empty()) {
    auto m = std::move(q->front());
    q->pop_front();

    out += "@";
    out += m.from;
    out += ": ";
    out += m.content;
    out += "\n";
  }
  return out;
}

std::vector<Message> ParseAgentMultiTargetOutput(const std::string& from,
                                                const std::string& text) {
  std::vector<Message> out;

  std::string cur_to;
  std::string cur;

  auto flush = [&]() {
    if (cur_to.empty()) return;
    out.push_back(Message{.from = from, .to = cur_to, .content = cur});
    cur_to.clear();
    cur.clear();
  };

  size_t start = 0;
  while (start <= text.size()) {
    size_t nl = text.find('\n', start);
    std::string line;
    if (nl == std::string::npos) {
      line = text.substr(start);
      start = text.size() + 1;
    } else {
      line = text.substr(start, nl - start);
      start = nl + 1;
    }

    if (nl == std::string::npos && line.empty()) break;

    if (!line.empty() && line[0] == '@') {
      const auto colon = line.find(':');
      if (colon == std::string::npos) continue;
      const std::string to = Trim(line.substr(1, colon - 1));
      if (to.empty()) continue;

      flush();
      cur_to = to;
      cur = Trim(line.substr(colon + 1));
      continue;
    }

    if (cur_to.empty()) continue;
    if (!cur.empty()) cur += "\n";
    cur += line;
  }

  flush();
  return out;
}

} // namespace agent
