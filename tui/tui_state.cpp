#include "tui_state.h"

namespace agent_cli {

void AppState::reset_view() {
  tool_expanded.clear();
  scroll_y = 1.0f;
  auto_scroll = true;
  last_snapshot_size = 0;
}

void AppState::clear_all() {
  chat_log.clear();
  tool_panel.clear();
  reset_view();
}

}  // namespace agent_cli
