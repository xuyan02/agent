#pragma once

#include <string>

namespace agent {

class IStorage {
public:
  virtual ~IStorage() = default;
  virtual void AppendLogLine(const std::string& line) = 0;
};

} // namespace agent
