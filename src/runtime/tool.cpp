#include "runtime/tool.h"

namespace agent {

Function* Tool::FindFunctionByName(const std::string& name) const {
  for (const auto& f : functions) {
    if (!f)
      continue;
    if (f->spec().name == name)
      return f.get();
  }
  return nullptr;
}

}  // namespace agent
