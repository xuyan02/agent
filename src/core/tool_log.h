#pragma once

#include <string>

namespace agent {

struct ToolCall;
struct ToolResult;

[[nodiscard]] std::string format_tool_log_line(const ToolCall& tc, const ToolResult& tr);

} // namespace agent
