#pragma once

#include "interfaces/istorage.h"

#include <filesystem>

namespace cpp_agent::infra::storage {

class JsonFileStorage final : public cpp_agent::interfaces::IStorage {
public:
  explicit JsonFileStorage(std::filesystem::path storage_dir);

  void append_log_line(const std::string& line) override;

private:
  std::filesystem::path storage_dir_;
  std::filesystem::path log_path_;
};

} // namespace cpp_agent::infra::storage
