#pragma once

#include "interfaces/istorage.h"

#include <filesystem>

namespace agent {

class JsonFileStorage final : public agent::IStorage {
public:
  explicit JsonFileStorage(std::filesystem::path storage_dir);

  void AppendLogLine(const std::string& line) override;

private:
  std::filesystem::path storage_dir_;
  std::filesystem::path log_path_;
};

} // namespace agent
