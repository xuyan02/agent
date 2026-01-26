#include "runtime/general_agent.h"

#include "runtime/runtime.h"

#include "dust/message_loop/message_loop.h"

#include <cctype>
#include <iostream>
#include <utility>

namespace agent {

static std::string Trim(std::string s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) b++;
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
  return s.substr(b, e - b);
}

GeneralAgent::GeneralAgent(Runtime& runtime,
                           std::string name,
                           agent::LlmContext& llm,
                           std::string model)
    : Agent(runtime, std::move(name)), llm_(llm), model_(std::move(model)) {}

GeneralAgent::~GeneralAgent() = default;

void GeneralAgent::Input(const Message& msg) {
  queue_.push_back(msg);
  TryStartRequest();
}

void GeneralAgent::TryStartRequest() {
  if (in_flight_) return;
  if (queue_.empty()) return;

  const std::string prompt = BuildBatchInput(queue_);

  in_flight_ = true;

  auto on_token = [this](std::string tok) { OnToken(tok); };

  auto on_done = [this]() { OnRequestDone(); };

  active_req_ = llm_.Create(model_, prompt, std::move(on_token), std::move(on_done));
  if (!active_req_) {
    std::cerr << "error: failed to create llm request\n";
    in_flight_ = false;
    return;
  }
}

void GeneralAgent::OnRequestDone() {
  in_flight_ = false;

  // Flush any pending output block even if the last line didn't end with '\n'.
  if (!out_buf_.empty()) {
    EmitParsedOutputLines(out_buf_);
    out_buf_.clear();
  }
  if (!current_to_.empty()) {
    runtime().Emit(Message{.from = name(), .to = current_to_, .content = current_content_});
    current_to_.clear();
    current_content_.clear();
  }

  // Next tick: try to process any queued messages accumulated while streaming.
  auto* loop = dust::MessageLoop::Current();
  if (!loop) {
    std::cerr << "error: no MessageLoop::Current()\n";
    return;
  }
  loop->task_runner()->PostTask([this]() { TryStartRequest(); });
}

std::string GeneralAgent::BuildBatchInput(std::deque<Message>& q) {
  std::string out;
  while (!q.empty()) {
    auto m = std::move(q.front());
    q.pop_front();

    out += "@";
    out += m.from;
    out += ": ";
    out += m.content;
    out += "\n";
  }
  return out;
}

void GeneralAgent::OnToken(const std::string& tok) {
  if (tok.empty()) return;
  out_buf_ += tok;

  // Process full lines.
  size_t start = 0;
  while (true) {
    const auto nl = out_buf_.find('\n', start);
    if (nl == std::string::npos) break;

    std::string line = out_buf_.substr(start, nl - start);
    start = nl + 1;

    EmitParsedOutputLines(line);
  }

  if (start > 0) out_buf_.erase(0, start);
}

void GeneralAgent::EmitParsedOutputLines(const std::string& raw_line) {
  const std::string line = raw_line;
  if (line.empty()) return;

  // Header line: @to: rest
  if (line[0] == '@') {
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      std::cerr << "error: invalid @to: header (dropped)\n";
      return;
    }

    const std::string to = Trim(line.substr(1, colon - 1));
    if (to.empty()) {
      std::cerr << "error: empty to in agent output (dropped)\n";
      return;
    }

    // Flush previous block.
    if (!current_to_.empty()) {
      runtime().Emit(Message{.from = name(), .to = current_to_, .content = current_content_});
    }

    current_to_ = to;
    current_content_ = line.substr(colon + 1);
    if (!current_content_.empty() && current_content_.front() == ' ') current_content_.erase(0, 1);
    return;
  }

  // Continuation line.
  if (current_to_.empty()) {
    std::cerr << "error: continuation without @to: header (dropped)\n";
    return;
  }

  if (!current_content_.empty()) current_content_ += "\n";
  current_content_ += line;
}

} // namespace agent
