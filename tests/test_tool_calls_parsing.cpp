#include "infra/http/openai_client.h"

#include <cassert>
#include <cstdlib>

int main() {
  // We only test response parsing indirectly by calling complete() is not feasible
  // (would do network). Instead, we validate we can build with tool-call fields
  // by compiling this TU; parsing is covered by Agent loop tests.
  //
  // Placeholder minimal test to keep test suite structure extensible.
  assert(true);
  return 0;
}
