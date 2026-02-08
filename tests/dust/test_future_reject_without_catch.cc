#include "dust/async/future.h"

#include <cassert>
#include <string>

namespace {

class RejectPromise final : public dust::Promise<void> {
 public:
  void Subscribe() override {
    Reject("boom");
  }
};

}  // namespace

int main() {
  bool then_called = false;

  // Should not crash even though Catch() is not registered.
  dust::MakeRefPtr<RejectPromise>()->Then([&]() { then_called = true; })->Subscribe();

  assert(!then_called);
  return 0;
}
