#pragma once

#include <string>

namespace cpp_agent::interfaces {

class IStorage {
public:
  virtual ~IStorage() = default;
  virtual void append_log_line(const std::string& line) = 0;
};

} // namespace cpp_agent::interfaces
