#include "tui_components.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

namespace agent_cli {

// ============================================================
// EntryKind
// ============================================================

std::string to_string(EntryKind kind) {
  switch (kind) {
    case EntryKind::UserMsg:
      return "UserMsg";
    case EntryKind::AssistantText:
      return "AssistantText";
    case EntryKind::ToolCall:
      return "ToolCall";
    case EntryKind::ToolResult:
      return "ToolResult";
    case EntryKind::SubtaskStart:
      return "SubtaskStart";
    case EntryKind::SubtaskEnd:
      return "SubtaskEnd";
    case EntryKind::Error:
      return "Error";
    case EntryKind::SystemInfo:
      return "SystemInfo";
  }
  return "Unknown";
}

// ============================================================
// ChatLog
// ============================================================

void ChatLog::push(ChatEntry entry) {
  std::lock_guard<std::mutex> lock(mu_);
  entries_.push_back(std::move(entry));
}

void ChatLog::append_stream(const std::string& delta) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!entries_.empty() && entries_.back().kind == EntryKind::AssistantText) {
    entries_.back().text += delta;
  } else {
    entries_.push_back({EntryKind::AssistantText, delta, ""});
  }
}

std::vector<ChatEntry> ChatLog::snapshot() const {
  std::lock_guard<std::mutex> lock(mu_);
  return entries_;
}

size_t ChatLog::size() const {
  std::lock_guard<std::mutex> lock(mu_);
  return entries_.size();
}

void ChatLog::clear() {
  std::lock_guard<std::mutex> lock(mu_);
  entries_.clear();
}

ChatEntry ChatLog::last() const {
  std::lock_guard<std::mutex> lock(mu_);
  if (entries_.empty()) return {EntryKind::SystemInfo, "", ""};
  return entries_.back();
}

std::vector<ChatEntry> ChatLog::filter(EntryKind kind) const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<ChatEntry> result;
  for (const auto& e : entries_) {
    if (e.kind == kind) result.push_back(e);
  }
  return result;
}

// ============================================================
// ToolPanel
// ============================================================

void ToolPanel::start_tool(const std::string& name, const std::string& args_summary) {
  std::lock_guard<std::mutex> lock(mu_);
  activities_.push_back({name, "running", args_summary});
}

void ToolPanel::finish_tool(const std::string& name, const std::string& result_summary, bool is_error) {
  std::lock_guard<std::mutex> lock(mu_);
  for (auto it = activities_.rbegin(); it != activities_.rend(); ++it) {
    if (it->tool_name == name && it->status == "running") {
      it->status = is_error ? "error" : "done";
      it->summary = result_summary;
      break;
    }
  }
}

std::vector<ToolActivity> ToolPanel::snapshot() const {
  std::lock_guard<std::mutex> lock(mu_);
  if (activities_.size() <= 50) return activities_;
  return {activities_.end() - 50, activities_.end()};
}

size_t ToolPanel::size() const {
  std::lock_guard<std::mutex> lock(mu_);
  return activities_.size();
}

std::string ToolPanel::tool_status(const std::string& name) const {
  std::lock_guard<std::mutex> lock(mu_);
  for (auto it = activities_.rbegin(); it != activities_.rend(); ++it) {
    if (it->tool_name == name) return it->status;
  }
  return "";
}

void ToolPanel::clear() {
  std::lock_guard<std::mutex> lock(mu_);
  activities_.clear();
}

// ============================================================
// 命令解析
// ============================================================

const std::vector<CommandDef>& command_defs() {
  static const std::vector<CommandDef> defs = {
      {"/quit", "/q", "退出程序", CommandType::Quit},
      {"/clear", "", "清空聊天记录", CommandType::Clear},
      {"/help", "/h", "显示帮助信息", CommandType::Help},
      {"/sessions", "/s", "管理会话", CommandType::Sessions},
      {"/compact", "", "压缩上下文", CommandType::Compact},
      {"/expand", "", "展开所有工具调用", CommandType::Expand},
      {"/collapse", "", "折叠所有工具调用", CommandType::Collapse},
  };
  return defs;
}

std::vector<CommandDef> match_commands(const std::string& prefix) {
  std::vector<CommandDef> result;
  if (prefix.empty() || prefix[0] != '/') return result;
  std::string lower_prefix = prefix;
  for (auto& c : lower_prefix) c = static_cast<char>(std::tolower(c));
  for (const auto& def : command_defs()) {
    if (def.name.substr(0, lower_prefix.size()) == lower_prefix ||
        (!def.shortcut.empty() && def.shortcut.substr(0, lower_prefix.size()) == lower_prefix)) {
      result.push_back(def);
    }
  }
  return result;
}

ParsedCommand parse_command(const std::string& input) {
  if (input.empty()) return {CommandType::None, ""};
  if (input[0] != '/') return {CommandType::None, ""};

  auto space_pos = input.find(' ');
  std::string cmd = (space_pos != std::string::npos) ? input.substr(0, space_pos) : input;
  std::string arg = (space_pos != std::string::npos) ? input.substr(space_pos + 1) : "";

  if (cmd == "/q" || cmd == "/quit") return {CommandType::Quit, arg};
  if (cmd == "/clear") return {CommandType::Clear, arg};
  if (cmd == "/h" || cmd == "/help") return {CommandType::Help, arg};
  if (cmd == "/s" || cmd == "/sessions") return {CommandType::Sessions, arg};
  if (cmd == "/compact") return {CommandType::Compact, arg};
  if (cmd == "/expand") return {CommandType::Expand, arg};
  if (cmd == "/collapse") return {CommandType::Collapse, arg};
  return {CommandType::Unknown, cmd};
}

// ============================================================
// 文本工具函数
// ============================================================

std::string truncate_text(const std::string& s, size_t max_len) {
  if (s.size() <= max_len) return s;
  return s.substr(0, max_len) + "...";
}

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream iss(text);
  std::string line;
  while (std::getline(iss, line)) {
    lines.push_back(line);
  }
  if (lines.empty()) lines.push_back("");
  return lines;
}

std::string format_time(const std::chrono::system_clock::time_point& ts) {
  auto time_t = std::chrono::system_clock::to_time_t(ts);
  std::tm tm{};
  localtime_r(&time_t, &tm);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  return buf;
}

std::string format_tokens(int64_t tokens) {
  if (tokens < 1000) return std::to_string(tokens);
  if (tokens < 1000000) {
    double k = static_cast<double>(tokens) / 1000.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1fK", k);
    return buf;
  }
  double m = static_cast<double>(tokens) / 1000000.0;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1fM", m);
  return buf;
}

// ============================================================
// AgentMode
// ============================================================

std::string to_string(AgentMode mode) {
  switch (mode) {
    case AgentMode::Build:
      return "build";
    case AgentMode::Plan:
      return "plan";
  }
  return "build";
}

// ============================================================
// AgentState
// ============================================================

void AgentState::set_running(bool running) {
  running_.store(running);
}

bool AgentState::is_running() const {
  return running_.load();
}

void AgentState::set_model(const std::string& model) {
  std::lock_guard<std::mutex> lock(mu_);
  model_ = model;
}

std::string AgentState::model() const {
  std::lock_guard<std::mutex> lock(mu_);
  return model_;
}

void AgentState::set_session_id(const std::string& id) {
  std::lock_guard<std::mutex> lock(mu_);
  session_id_ = id;
}

std::string AgentState::session_id() const {
  std::lock_guard<std::mutex> lock(mu_);
  return session_id_;
}

void AgentState::update_tokens(int64_t input, int64_t output) {
  input_tokens_.store(input);
  output_tokens_.store(output);
}

int64_t AgentState::input_tokens() const {
  return input_tokens_.load();
}

int64_t AgentState::output_tokens() const {
  return output_tokens_.load();
}

void AgentState::set_activity(const std::string& msg) {
  std::lock_guard<std::mutex> lock(mu_);
  activity_ = msg;
}

std::string AgentState::activity() const {
  std::lock_guard<std::mutex> lock(mu_);
  return activity_;
}

void AgentState::set_mode(AgentMode mode) {
  mode_.store(static_cast<int>(mode));
}

AgentMode AgentState::mode() const {
  return static_cast<AgentMode>(mode_.load());
}

void AgentState::toggle_mode() {
  auto current = mode();
  set_mode(current == AgentMode::Build ? AgentMode::Plan : AgentMode::Build);
}

std::string AgentState::status_text() const {
  std::string s = "Model: " + model();
  s += " | Tokens: " + format_tokens(input_tokens()) + "in/" + format_tokens(output_tokens()) + "out";
  s += is_running() ? " | [Running...]" : " | [Ready]";
  return s;
}

}  // namespace agent_cli
