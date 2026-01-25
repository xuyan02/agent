#pragma once

#include "infra/llm/llm_context.h"

#include <filesystem>

namespace cpp_agent::infra::llm {

// Loads providers from a JSON config file and registers them into ctx.
//
// Expected schema:
// {
//   "providers": [
//     {
//       "name": "openai",
//       "models": ["gpt-4o", "gpt-4.1"],
//       "params": { ... }
//     }
//   ]
// }
bool RegisterProvidersFromConfig(LlmContext& ctx, const std::filesystem::path& path);

} // namespace cpp_agent::infra::llm
