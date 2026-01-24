#pragma once

#include <string>

namespace cpp_agent::core {

struct ToolCall;
struct ToolResult;

[[nodiscard]] std::string format_tool_log_line(const ToolCall& tc, const ToolResult& tr);

} // namespace cpp_agent::core
