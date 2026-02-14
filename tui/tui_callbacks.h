#pragma once

// tui_callbacks.h — Session 回调设置与历史消息加载

#include <memory>

#include "agent/agent.hpp"

namespace agent_cli {

// 前置声明
struct AppState;
struct AppContext;

// 设置 session 的所有 TUI 回调
void setup_tui_callbacks(AppState& state, AppContext& ctx);

// 将 session 历史消息回填到 ChatLog（用于会话恢复）
void load_history_to_chat_log(AppState& state, const std::shared_ptr<agent::Session>& session);

}  // namespace agent_cli
