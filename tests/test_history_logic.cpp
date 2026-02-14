#include <gtest/gtest.h>

#include <string>
#include <vector>

// 模拟 TUI 输入历史管理逻辑
class HistoryManager {
 public:
  std::vector<std::string> input_history;
  int history_index = -1;
  std::string temp_text;

  void add_to_history(const std::string& input) {
    if (!input.empty()) {
      if (input_history.empty() || input_history.back() != input) {
        input_history.push_back(input);
      }
    }
    history_index = -1;
    temp_text.clear();
  }

  bool handle_up_arrow(std::string& current_input) {
    if (input_history.empty()) {
      return true;  // 拦截但不做任何事
    }

    if (history_index == -1) {
      temp_text = current_input;
      history_index = static_cast<int>(input_history.size()) - 1;
    } else if (history_index > 0) {
      history_index--;
    }

    current_input = input_history[history_index];
    return true;
  }

  bool handle_down_arrow(std::string& current_input) {
    if (history_index == -1) {
      return true;  // 已经在当前输入
    }

    if (history_index < static_cast<int>(input_history.size()) - 1) {
      history_index++;
      current_input = input_history[history_index];
    } else {
      history_index = -1;
      current_input = temp_text;
      temp_text.clear();
    }
    return true;
  }
};

class HistoryLogicTest : public ::testing::Test {
 protected:
  HistoryManager manager;
  std::string current_input;
};

TEST_F(HistoryLogicTest, EmptyHistoryUpArrow) {
  current_input = "typing...";
  manager.handle_up_arrow(current_input);

  // 没有历史时，输入不变
  EXPECT_EQ(current_input, "typing...");
  EXPECT_EQ(manager.history_index, -1);
}

TEST_F(HistoryLogicTest, AddToHistory) {
  manager.add_to_history("first");
  manager.add_to_history("second");
  manager.add_to_history("third");

  EXPECT_EQ(manager.input_history.size(), 3);
  EXPECT_EQ(manager.input_history[0], "first");
  EXPECT_EQ(manager.input_history[2], "third");
}

TEST_F(HistoryLogicTest, AddDuplicateNotAllowed) {
  manager.add_to_history("same");
  manager.add_to_history("same");

  EXPECT_EQ(manager.input_history.size(), 1);
}

TEST_F(HistoryLogicTest, AddEmptyStringIgnored) {
  manager.add_to_history("");
  manager.add_to_history("valid");
  manager.add_to_history("");

  EXPECT_EQ(manager.input_history.size(), 1);
  EXPECT_EQ(manager.input_history[0], "valid");
}

TEST_F(HistoryLogicTest, NavigateUpThroughHistory) {
  manager.add_to_history("first");
  manager.add_to_history("second");
  manager.add_to_history("third");
  current_input.clear();

  manager.handle_up_arrow(current_input);
  EXPECT_EQ(current_input, "third");

  manager.handle_up_arrow(current_input);
  EXPECT_EQ(current_input, "second");

  manager.handle_up_arrow(current_input);
  EXPECT_EQ(current_input, "first");

  // 再按上键应该停在最早的记录
  manager.handle_up_arrow(current_input);
  EXPECT_EQ(current_input, "first");
}

TEST_F(HistoryLogicTest, NavigateDownThroughHistory) {
  manager.add_to_history("first");
  manager.add_to_history("second");
  manager.add_to_history("third");
  current_input.clear();

  // 先上到最早
  manager.handle_up_arrow(current_input);
  manager.handle_up_arrow(current_input);
  manager.handle_up_arrow(current_input);
  EXPECT_EQ(current_input, "first");

  // 然后下来
  manager.handle_down_arrow(current_input);
  EXPECT_EQ(current_input, "second");

  manager.handle_down_arrow(current_input);
  EXPECT_EQ(current_input, "third");

  // 再按下键应该恢复到临时保存的输入
  manager.handle_down_arrow(current_input);
  EXPECT_EQ(current_input, "");
  EXPECT_EQ(manager.history_index, -1);
}

TEST_F(HistoryLogicTest, PreserveCurrentInputOnUpArrow) {
  manager.add_to_history("history1");
  manager.add_to_history("history2");
  current_input = "typing something";

  manager.handle_up_arrow(current_input);
  EXPECT_EQ(current_input, "history2");
  EXPECT_EQ(manager.temp_text, "typing something");

  // 下箭头回到当前输入
  manager.handle_down_arrow(current_input);
  EXPECT_EQ(current_input, "typing something");
}

TEST_F(HistoryLogicTest, DownArrowWithoutUpFirst) {
  manager.add_to_history("history");
  current_input = "current";

  // 没有先按上键，按下键不应该改变任何东西
  manager.handle_down_arrow(current_input);
  EXPECT_EQ(current_input, "current");
  EXPECT_EQ(manager.history_index, -1);
}

TEST_F(HistoryLogicTest, ResetIndexAfterSubmit) {
  manager.add_to_history("first");
  manager.add_to_history("second");
  current_input.clear();

  manager.handle_up_arrow(current_input);
  EXPECT_EQ(manager.history_index, 1);

  // 模拟提交
  manager.add_to_history("third");

  EXPECT_EQ(manager.history_index, -1);
  EXPECT_TRUE(manager.temp_text.empty());
}
