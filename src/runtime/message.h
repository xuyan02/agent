#pragma once

#include <string>

namespace agent {

struct Message {
  std::string from;
  std::string to;
  std::string content;
};

} // namespace agent
