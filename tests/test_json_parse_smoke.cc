#include "json/json.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <iostream>

static void test_parse_invalid_returns_nullopt() {
  assert(!agent::json::Parse("{not json"));
}

static void test_parse_and_get_string() {
  auto j = agent::json::Parse(R"({"a":"b","arr":["x","y"]})");
  assert(j.has_value());

  auto a = agent::json::GetString(*j, "a");
  assert(a.has_value());
  assert(*a == "b");

  auto arr = agent::json::GetStringArrayAllowMissing(*j, "arr");
  assert(arr.has_value());
  assert(arr->size() == 2u);
  assert((*arr)[0] == "x");
  assert((*arr)[1] == "y");
}

int main() {
  test_parse_invalid_returns_nullopt();
  test_parse_and_get_string();
  std::cout << "ok\n";
  return 0;
}
