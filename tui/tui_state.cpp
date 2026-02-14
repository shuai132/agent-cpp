#include "tui_state.h"

namespace agent_cli {

void AppState::reset_view() {
  tool_expanded.clear();
  scroll_y = 1.0f;
  auto_scroll = true;
  last_snapshot_size = 0;
  show_file_path_menu = false;
  file_path_matches.clear();
  file_path_menu_selected = 0;
}

void AppState::clear_all() {
  chat_log.clear();
  tool_panel.clear();
  reset_view();
}

void AppState::reset_question_panel() {
  show_question_panel = false;
  question_list.clear();
  question_answers.clear();
  question_current_index = 0;
  question_input_text.clear();
  question_promise.reset();
}

}  // namespace agent_cli
