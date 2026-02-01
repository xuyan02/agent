#pragma once

#include "agent/routing_result.h"

#include <optional>
#include <string>

namespace agent {

std::optional<RoutingResult> ParseRoutingResultFromJson(const std::string& s);

}  // namespace agent
