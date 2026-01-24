#include "infra/storage/json_file_storage.h"

#include "core/errors.h"

#include <chrono>
#include <fstream>

namespace cpp_agent::infra::storage {

static std::filesystem::path expand_home(std::filesystem::path p) {
  auto s = p.string();
  if (s.rfind("~/", 0) == 0) {
    const char* home = std::getenv("HOME");
    if (home) {
      return std::filesystem::path(home) / s.substr(2);
    }
  }
  return p;
}

JsonFileStorage::JsonFileStorage(std::filesystem::path storage_dir)
    : storage_dir_(expand_home(std::move(storage_dir))) {
  std::error_code ec;
  std::filesystem::create_directories(storage_dir_, ec);
  if (ec) {
    throw cpp_agent::core::AgentError(cpp_agent::core::ErrorCode::kIo,
                                     "Failed to create storage dir" );
  }
  log_path_ = storage_dir_ / "logs.txt";
}

void JsonFileStorage::append_log_line(const std::string& line) {
  std::ofstream ofs(log_path_, std::ios::app);
  if (!ofs) return;
  ofs << line << "\n";
}

} // namespace cpp_agent::infra::storage
