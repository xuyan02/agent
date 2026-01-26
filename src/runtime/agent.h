#pragma once

#include <string>

namespace agent {
class Runtime;
}

namespace agent {

class Agent {
public:
  Agent(Runtime& runtime, std::string name);
  virtual ~Agent();

  std::string name() const;

protected:
  Runtime& runtime();
  const Runtime& runtime() const;

private:
  Runtime& runtime_;
  std::string name_;
};

} // namespace agent
