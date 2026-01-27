#include "infra/llm/json_min.h"

#include <cassert>

int main() {
  assert(agent::json_unescape("\\n") == "\n");
  assert(agent::json_unescape("a\\nb") == "a\nb");
  assert(agent::json_unescape("a\\rb") == "a\rb");
  assert(agent::json_unescape("a\\tb") == "a\tb");
  assert(agent::json_unescape("\\\"") == "\"");
  assert(agent::json_unescape("\\\\") == "\\");
  return 0;
}
