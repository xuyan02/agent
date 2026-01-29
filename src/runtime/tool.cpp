#include "runtime/tool.h"

namespace agent {

FunctionPtr Tool::FindFunctionByName(const std::string& name) const {
  for (const auto& f : functions) {
    if (!f) continue;
    if (f->spec().name == name) return f;
  }
  return nullptr;
}

} // namespace agent
