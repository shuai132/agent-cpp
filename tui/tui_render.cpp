#include "tui_render.h"

#include <filesystem>
#include <string>
#include <vector>

#include <ftxui/screen/color.hpp>

namespace agent_cli {

using namespace ftxui;

// ============================================================
// 聊天条目渲染
// ============================================================

Element render_text_entry(const ChatEntry& entry) {
  switch (entry.kind) {
    case EntryKind::UserMsg:
      return vbox({
          hbox({text("  ❯ ") | color(Color::Green), text("You") | bold | color(Color::Green)}),
          hbox({text("    "), paragraph(entry.text)}),
          text(""),
      });

    case EntryKind::AssistantText: {
      auto lines = split_lines(entry.text);
      Elements content;
      for (const auto& line : lines) {
        content.push_back(paragraph(line));
      }
      return vbox({
          hbox({text("  ✦ ") | color(Color::Cyan), text("AI") | bold | color(Color::Cyan)}),
          hbox({text("    "), vbox(content) | flex}) | flex,
          text(""),
      });
    }

    case EntryKind::SubtaskStart:
      return hbox({
          text("    ◈ Subtask: ") | color(Color::Magenta) | bold,
          text(entry.text) | color(Color::Magenta),
      });

    case EntryKind::SubtaskEnd:
      return hbox({
          text("    ◈ Done: ") | color(Color::Magenta),
          text(truncate_text(entry.text, 100)) | dim,
      });

    case EntryKind::Error:
      return hbox({
          text("  ✗ ") | color(Color::Red) | bold,
          paragraph(entry.text) | color(Color::Red),
      });

    case EntryKind::SystemInfo: {
      auto lines = split_lines(entry.text);
      Elements elems;
      for (const auto& line : lines) {
        elems.push_back(hbox({text("  "), text(line) | dim}));
      }
      return vbox(elems);
    }

    default:
      return text("");
  }
}

// ============================================================
// 工具调用卡片渲染
// ============================================================

Element render_tool_group(const ToolGroup& group, bool expanded) {
  bool is_error = group.has_result && group.result.text.find("✗") != std::string::npos;
  bool is_running = !group.has_result;

  std::string status_icon = is_running ? "⏳" : (is_error ? "✗" : "✓");
  Color status_color = is_running ? Color::Yellow : (is_error ? Color::Red : Color::Green);

  // 卡片头部
  std::string header_text = group.call.text;
  if (!expanded && group.has_result) {
    auto first_line = group.result.detail.substr(0, group.result.detail.find('\n'));
    auto summary = truncate_text(first_line, 80);
    if (!summary.empty()) header_text += "  " + summary;
  }
  if (is_running) header_text += "  running...";

  auto header_line = hbox({
      text(" " + status_icon + "  ") | color(status_color),
      text(header_text) | (is_running ? dim : bold),
  });

  if (!expanded) {
    return hbox({text(" "), vbox({header_line}) | borderRounded});
  }

  // 展开模式
  Elements card_content;
  card_content.push_back(header_line);
  card_content.push_back(text(""));

  // 参数区域
  auto args_lines = split_lines(group.call.detail);
  card_content.push_back(text("   Arguments:") | bold | dim);
  for (size_t i = 0; i < args_lines.size() && i < 20; ++i) {
    card_content.push_back(text("   " + args_lines[i]) | dim);
  }
  if (args_lines.size() > 20) {
    card_content.push_back(text("   ...(" + std::to_string(args_lines.size()) + " lines)") | dim);
  }

  // 结果区域
  if (group.has_result) {
    card_content.push_back(text(""));
    card_content.push_back(text(is_error ? "   Error:" : "   Result:") | bold | dim | color(status_color));
    auto result_lines = split_lines(group.result.detail);
    for (size_t i = 0; i < result_lines.size() && i < 30; ++i) {
      card_content.push_back(text("   " + result_lines[i]) | dim);
    }
    if (result_lines.size() > 30) {
      card_content.push_back(text("   ...(" + std::to_string(result_lines.size()) + " lines total)") | dim);
    }
  }

  return hbox({text(" "), vbox(card_content) | borderRounded});
}

// ============================================================
// 聊天视图构建
// ============================================================

Element build_chat_view(AppState& state) {
  auto entries = state.chat_log.snapshot();

  // 检测内容变化，自动滚动到底部
  size_t current_size = entries.size();
  bool content_changed = (current_size != state.last_snapshot_size);
  if (!content_changed && !entries.empty() && entries.back().kind == EntryKind::AssistantText) {
    content_changed = true;  // 流式追加
  }
  state.last_snapshot_size = current_size;

  if (state.auto_scroll && content_changed) {
    state.scroll_y = 1.0f;
  }

  Elements chat_elements;
  chat_elements.push_back(text(""));

  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& e = entries[i];

    if (e.kind == EntryKind::ToolCall) {
      ToolGroup group;
      group.call = e;
      if (i + 1 < entries.size() && entries[i + 1].kind == EntryKind::ToolResult) {
        group.result = entries[i + 1];
        group.has_result = true;
      }
      bool expanded = state.tool_expanded.count(i) && state.tool_expanded[i];
      chat_elements.push_back(render_tool_group(group, expanded));
      continue;
    }

    if (e.kind == EntryKind::ToolResult && i > 0 && entries[i - 1].kind == EntryKind::ToolCall) {
      continue;  // 已配对的 ToolResult
    }

    chat_elements.push_back(render_text_entry(e));
  }

  // 活动状态文字
  if (state.agent_state.is_running()) {
    auto activity = state.agent_state.activity();
    if (activity.empty()) activity = "Thinking...";
    chat_elements.push_back(hbox({text("    "), text(activity) | dim | color(Color::Cyan)}));
  }

  chat_elements.push_back(text(""));

  return vbox(chat_elements)                            //
         | focusPositionRelative(0.f, state.scroll_y)   //
         | vscroll_indicator                            //
         | yframe                                       //
         | flex;
}

// ============================================================
// 状态栏
// ============================================================

Element build_status_bar(const AppState& state) {
  return hbox({
      text(" " + std::filesystem::current_path().filename().string() + " ") | bold | color(Color::White) | bgcolor(Color::Blue),
      text(" "),
      text(state.agent_state.model()) | dim,
      filler(),
      text(format_tokens(state.agent_state.input_tokens()) + "↑ " + format_tokens(state.agent_state.output_tokens()) + "↓") | dim,
      text(" "),
      text(state.agent_state.is_running() ? " ● Running " : " ● Ready ") | color(Color::White) |
          bgcolor(state.agent_state.is_running() ? Color::Yellow : Color::Green),
  });
}

// ============================================================
// 命令提示菜单
// ============================================================

Element build_cmd_menu(const AppState& state) {
  if (!state.show_cmd_menu || state.input_text.empty()) return text("");

  auto matches = match_commands(state.input_text);
  if (matches.empty()) return text("");

  Elements menu_items;
  for (int j = 0; j < static_cast<int>(matches.size()); ++j) {
    auto& def = matches[j];
    bool selected = (j == state.cmd_menu_selected);
    auto item = hbox({
        text("  "),
        text(def.name) | bold,
        text(def.shortcut.empty() ? "" : " (" + def.shortcut + ")") | dim,
        text("  "),
        text(def.description) | dim,
    });
    if (selected) {
      item = item | bgcolor(Color::GrayDark) | color(Color::White);
    }
    menu_items.push_back(item);
  }
  return vbox(menu_items) | borderRounded | color(Color::GrayLight);
}

// ============================================================
// 会话列表面板
// ============================================================

Element build_sessions_panel(AppState& state) {
  Elements session_items;
  if (state.sessions_cache.empty()) {
    session_items.push_back(text("  No saved sessions") | dim);
  } else {
    for (int si = 0; si < static_cast<int>(state.sessions_cache.size()); ++si) {
      const auto& meta = state.sessions_cache[si];
      bool is_current = (meta.id == state.agent_state.session_id());
      bool is_selected = (si == state.sessions_selected);
      std::string title = meta.title.empty() ? "(untitled)" : meta.title;
      std::string marker = is_current ? " ●" : "  ";
      std::string detail =
          format_time(meta.updated_at) + "  " + agent::to_string(meta.agent_type) + "  tokens: " + format_tokens(meta.total_usage.total());

      auto row = vbox({
          hbox({
              text(marker) | color(Color::Green),
              text(" " + std::to_string(si + 1) + ". ") | dim,
              text(title) | bold,
          }),
          hbox({text("      "), text(detail) | dim}),
      });

      if (is_selected) {
        row = row | bgcolor(Color::GrayDark) | color(Color::White);
      }
      session_items.push_back(row);
      session_items.push_back(text(""));
    }
  }

  // reflect 捕获屏幕坐标
  state.session_item_boxes.resize(state.sessions_cache.size());
  Elements reflected_items;
  for (size_t ri = 0; ri + 1 < session_items.size(); ri += 2) {
    int idx = static_cast<int>(ri / 2);
    if (idx < static_cast<int>(state.session_item_boxes.size())) {
      auto item = vbox({session_items[ri], session_items[ri + 1]}) | reflect(state.session_item_boxes[idx]);
      if (idx == state.sessions_selected) {
        item = item | focus;
      }
      reflected_items.push_back(item);
    }
  }
  if (session_items.size() % 2 == 1) {
    reflected_items.push_back(session_items.back());
  }

  auto session_list = vbox(reflected_items)  //
                      | vscroll_indicator    //
                      | yframe               //
                      | flex;

  auto panel_header = hbox({
      text(" Sessions ") | bold,
      filler(),
      text(" ↑↓ navigate  Enter load  d delete  n new  Esc close ") | dim,
  });

  return vbox({panel_header, separator() | dim, session_list | flex});
}

}  // namespace agent_cli
