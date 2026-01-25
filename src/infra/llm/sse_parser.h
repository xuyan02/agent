#pragma once

#include <string>
#include <vector>

namespace cpp_agent::infra::llm {

class SseParser {
public:
  void Feed(const char* data, size_t n);

  // Pops complete SSE data payloads.
  // Each returned string is the concatenated payload for one event (data: lines joined with \n).
  std::vector<std::string> PopEvents();

private:
  void ConsumeLine(std::string line);

  std::string buf_;
  std::vector<std::string> ready_;

  std::string cur_data_;
};

} // namespace cpp_agent::infra::llm
