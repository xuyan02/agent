#include "runtime/plan2/plan2_model.h"

#include <cassert>
#include <string>

namespace {

using agent::plan2::AddTaskInput;
using agent::plan2::PlanModel;
using agent::plan2::Status;

static AddTaskInput top(std::string title, std::string report_to) {
  AddTaskInput in;
  in.title = std::move(title);
  in.report_to = std::move(report_to);
  return in;
}

} // namespace

int main() {
  PlanModel plan;

  // top-level must have report_to
  {
    AddTaskInput in;
    in.title = "t";
    auto r = plan.AddTasks({in});
    assert(!r.error.empty());
  }

  // create two top-level tasks
  auto r1 = plan.AddTasks({top("A", "master"), top("B", "master")});
  assert(r1.error.empty());
  assert(r1.created.size() == 2);
  const std::string a = r1.created[0].id;
  const std::string b = r1.created[1].id;

  // dependent task starts not_ready if dep not done
  AddTaskInput c_in = top("C", "master");
  c_in.depends_on = {a};
  auto r2 = plan.AddTasks({c_in});
  assert(r2.error.empty());
  assert(r2.created.size() == 1);
  assert(r2.created[0].status == Status::kNotReady);

  // Setting A to canceled cascades cancel to C.
  auto r3 = plan.SetStatus(a, Status::kCanceled, "");
  assert(r3.error.empty());
  assert(!r3.canceled_cascade.empty());

  return 0;
}
