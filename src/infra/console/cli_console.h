#pragma once

#include "interfaces/iconsole.h"

#include "dust/message_loop/message_loop.h"

#include <string>

namespace agent {

class CliConsole final : public agent::IConsole {
public:
  void PrintLine(const std::string& s) override;
  void Print(const std::string& s) override;
  void SetOnLine(dust::Function<void(std::string)> on_line) override;

  // Starts watching stdin (fd=0) on the given loop.
  void AttachToMessageLoop(dust::MessageLoop& loop);

private:
  void OnStdinReadable();

  dust::MessageLoop* loop_ = nullptr;
  dust::Function<void(std::string)> on_line_;
  std::string buffer_;
};

} // namespace agent
