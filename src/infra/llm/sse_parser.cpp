#include "infra/llm/sse_parser.h"

#include <utility>

namespace agent {

void SseParser::Feed(const char* data, size_t n) {
  buf_.append(data, n);

  for (;;) {
    auto pos = buf_.find('\n');
    if (pos == std::string::npos) break;

    std::string line = buf_.substr(0, pos);
    buf_.erase(0, pos + 1);

    if (!line.empty() && line.back() == '\r') line.pop_back();
    ConsumeLine(std::move(line));
  }
}

std::vector<std::string> SseParser::PopEvents() {
  std::vector<std::string> out;
  out.swap(ready_);
  return out;
}

void SseParser::ConsumeLine(std::string line) {
  if (line.empty()) {
    if (!cur_data_.empty()) {
      ready_.push_back(std::move(cur_data_));
      cur_data_.clear();
    }
    return;
  }

  // Only handle data: lines.
  constexpr const char* kPrefix = "data:";
  if (line.rfind(kPrefix, 0) != 0) return;

  std::string payload = line.substr(5);
  // Strip a single leading space.
  if (!payload.empty() && payload[0] == ' ') payload.erase(0, 1);

  if (!cur_data_.empty()) cur_data_.push_back('\n');
  cur_data_ += payload;
}

} // namespace agent
