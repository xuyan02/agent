
#include <cassert>
#include <string>

#include "runtime/plan2/plan2_model.h"

namespace {

using agent::plan2::AddSubtaskInput;
using agent::plan2::AddTaskInput;
using agent::plan2::PlanModel;
using agent::plan2::Status;

static AddTaskInput top(std::string name, std::string report_to) {
  AddTaskInput in;
  in.name = std::move(name);
  in.description = "desc";
  in.goal = "DoD: ok";
  in.report_to = std::move(report_to);
  return in;
}

} // namespace

int main() {
  PlanModel plan;

  // top-level must have report_to
  {
    AddTaskInput in;
    in.name = "t";
    in.description = "d";
    in.goal = "DoD: ok";
    auto r = plan.AddTask(in);
    assert(!r.error.empty());
  }

  // create two top-level tasks
  auto r1a = plan.AddTask(top("A", "master"));
  assert(r1a.error.empty());
  auto r1b = plan.AddTask(top("B", "master"));
  assert(r1b.error.empty());
  const std::string a = r1a.created.id;
  const std::string b = r1b.created.id;
  (void)b;

  // dependent task starts blocked if dep not done
  {
    AddTaskInput c_in = top("C", "master");
    c_in.depends_on = {a};
    auto r2 = plan.AddTask(c_in);
    assert(r2.error.empty());
    assert(r2.created.status == Status::kBlocked);
  }

  // completing A unblocks C (normalize siblings)
  {
    auto act = plan.ActivateTask(a);
    assert(act.error.empty());
    auto done = plan.CompleteTask(a);
    assert(done.error.empty());
  }

  // subtask activation auto-activates ancestors
  {
    // Create top-level D with a subtask.
    auto rd = plan.AddTask(top("D", "master"));
    assert(rd.error.empty());
    const std::string d = rd.created.id;

    AddSubtaskInput s;
    s.parent_id = d;
    s.name = "d1";
    s.description = "desc";
    s.goal = "DoD: ok";
    auto rs = plan.AddSubtask(s);
    assert(rs.error.empty());

    // Activate subtask; should also activate D.
    auto act = plan.ActivateTask(rs.created.id);
    assert(act.error.empty());
  }

  return 0;
}
