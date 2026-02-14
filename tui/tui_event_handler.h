#pragma once

// tui_event_handler.h — 事件处理与命令提交逻辑

#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>

namespace agent_cli {

// 前置声明
struct AppState;
struct AppContext;

// 命令提交处理（Enter 键）
void handle_submit(AppState& state, AppContext& ctx, ftxui::ScreenInteractive& screen);

// 会话命令处理（/s, /sessions）
void handle_sessions_command(AppState& state, AppContext& ctx, const std::string& arg);

// 会话面板事件处理
bool handle_sessions_panel_event(AppState& state, AppContext& ctx, ftxui::Event event);

// 主事件处理
bool handle_main_event(AppState& state, AppContext& ctx, ftxui::ScreenInteractive& screen, ftxui::Event event);

}  // namespace agent_cli
