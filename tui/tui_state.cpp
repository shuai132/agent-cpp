#include "tui_state.h"

#include <fstream>
#include <iostream>

namespace agent_cli {

void AppState::reset_view() {
  scroll_y = 1.0f;
  auto_scroll = true;
  last_snapshot_size = 0;
}

void AppState::clear_all() {
  chat_log.clear();
  tool_panel.clear();
  tool_expanded.clear();
  tool_boxes.clear();
  tool_entry_indices.clear();
  reset_view();
}

void AppState::reset_question_panel() {
  show_question_panel = false;
  question_list.clear();
  question_answers.clear();
  question_current_index = 0;
  question_input_text.clear();
  question_promise = nullptr;
}

void AppState::save_history_to_file(const std::filesystem::path& filepath) {
  nlohmann::json j;
  j["input_history"] = input_history;
  j["history_max_size"] = 100;  // 限制历史记录的最大数量

  // 为了防止文件过大，只保存最近的100条记录
  if (input_history.size() > 100) {
    std::vector<std::string> recent_history(input_history.end() - 100, input_history.end());
    j["input_history"] = recent_history;
  } else {
    j["input_history"] = input_history;
  }

  try {
    std::ofstream file(filepath);
    file << j.dump(2);  // 格式化输出，缩进2个空格
  } catch (const std::exception& e) {
    std::cerr << "Error saving history to file: " << e.what() << std::endl;
  }
}

void AppState::load_history_from_file(const std::filesystem::path& filepath) {
  if (!std::filesystem::exists(filepath)) {
    // 文件不存在，无需加载
    return;
  }

  try {
    std::ifstream file(filepath);
    nlohmann::json j;
    file >> j;

    if (j.contains("input_history") && j["input_history"].is_array()) {
      input_history = j["input_history"].get<std::vector<std::string>>();
    }
  } catch (const std::exception& e) {
    std::cerr << "Error loading history from file: " << e.what() << std::endl;
  }
}

}  // namespace agent_cli
