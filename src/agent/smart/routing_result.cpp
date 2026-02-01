#include "agent/smart/routing_result.h"

#include "agent/smart/routing_result_parse.h"

#include "infra/json/json.h"

#include <optional>
#include <string>

namespace agent {

namespace {

std::optional<RoutingResult::Outcome> ParseOutcome(const std::string& s) {
  if (s == "answer")
    return RoutingResult::Outcome::kAnswer;
  if (s == "shallow")
    return RoutingResult::Outcome::kShallow;
  if (s == "deep")
    return RoutingResult::Outcome::kDeep;
  return std::nullopt;
}

}  // namespace

std::optional<RoutingResult> ParseRoutingResultFromJson(const std::string& s) {
  auto j = agent::json::Parse(s);
  if (!j || !j->is_object())
    return std::nullopt;

  auto outcome_s = agent::json::GetString(*j, "outcome");
  auto content = agent::json::GetStringAllowMissing(*j, "content");
  auto reason = agent::json::GetStringAllowMissing(*j, "reason");

  if (!outcome_s)
    return std::nullopt;

  auto outcome = ParseOutcome(*outcome_s);
  if (!outcome)
    return std::nullopt;

  RoutingResult rr;
  rr.outcome = *outcome;
  rr.content = content.value_or(std::string{});
  rr.reason = reason.value_or(std::string{});

  if (rr.outcome != RoutingResult::Outcome::kAnswer && !rr.content.empty())
    return std::nullopt;

  return rr;
}

}  // namespace agent
