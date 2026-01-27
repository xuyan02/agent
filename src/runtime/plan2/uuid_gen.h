#pragma once

#include <string>

namespace agent::plan2 {

// Generates a random UUID v4 string (xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx).
//
// This implementation avoids external dependencies (libuuid/boost) to keep the
// project self-contained.
std::string GenerateUuidV4();

} // namespace agent::plan2
