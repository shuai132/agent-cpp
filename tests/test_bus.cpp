#include <gtest/gtest.h>

#include <atomic>
#include <string>
#include <vector>

#include "bus/bus.hpp"

using namespace agent;
using namespace agent::events;

// 每个测试结束后清理所有订阅，避免单例状态泄漏到其他测试
class BusTest : public ::testing::Test {
 protected:
  void TearDown() override {
    for (auto id : subscriptions_) {
      Bus::instance().unsubscribe(id);
    }
    subscriptions_.clear();
  }

  // 辅助方法：记录订阅 ID 以便自动清理
  Bus::SubscriptionId track(Bus::SubscriptionId id) {
    subscriptions_.push_back(id);
    return id;
  }

  std::vector<Bus::SubscriptionId> subscriptions_;
};

// 1. 验证单例
TEST_F(BusTest, Singleton) {
  auto& bus1 = Bus::instance();
  auto& bus2 = Bus::instance();
  EXPECT_EQ(&bus1, &bus2);
}

// 2. 订阅并发布事件
TEST_F(BusTest, SubscribeAndPublish) {
  std::string received_id;
  auto sub = track(Bus::instance().subscribe<SessionCreated>([&](const SessionCreated& e) {
    received_id = e.session_id;
  }));

  Bus::instance().publish(SessionCreated{.session_id = "sess_001"});

  EXPECT_EQ(received_id, "sess_001");
}

// 3. 多个订阅者收到同一事件
TEST_F(BusTest, MultipleSubscribers) {
  int call_count = 0;
  std::string received_a;
  std::string received_b;

  track(Bus::instance().subscribe<SessionCreated>([&](const SessionCreated& e) {
    received_a = e.session_id;
    ++call_count;
  }));

  track(Bus::instance().subscribe<SessionCreated>([&](const SessionCreated& e) {
    received_b = e.session_id;
    ++call_count;
  }));

  Bus::instance().publish(SessionCreated{.session_id = "sess_multi"});

  EXPECT_EQ(call_count, 2);
  EXPECT_EQ(received_a, "sess_multi");
  EXPECT_EQ(received_b, "sess_multi");
}

// 4. 取消订阅后不再收到事件
TEST_F(BusTest, Unsubscribe) {
  int call_count = 0;
  auto sub = Bus::instance().subscribe<SessionEnded>([&](const SessionEnded&) {
    ++call_count;
  });

  Bus::instance().publish(SessionEnded{.session_id = "sess_end"});
  EXPECT_EQ(call_count, 1);

  Bus::instance().unsubscribe(sub);

  Bus::instance().publish(SessionEnded{.session_id = "sess_end_2"});
  EXPECT_EQ(call_count, 1);  // 不应再增加
}

// 5. 不同类型事件互不干扰
TEST_F(BusTest, TypeSafety) {
  bool session_handler_called = false;
  bool stream_handler_called = false;

  track(Bus::instance().subscribe<SessionCreated>([&](const SessionCreated&) {
    session_handler_called = true;
  }));

  track(Bus::instance().subscribe<StreamDelta>([&](const StreamDelta&) {
    stream_handler_called = true;
  }));

  // 只发布 StreamDelta，SessionCreated 的处理器不应被调用
  Bus::instance().publish(StreamDelta{.session_id = "sess_1", .text = "hello"});

  EXPECT_FALSE(session_handler_called);
  EXPECT_TRUE(stream_handler_called);
}

// 6. 无订阅者时发布不崩溃
TEST_F(BusTest, PublishWithNoSubscribers) {
  EXPECT_NO_THROW(Bus::instance().publish(SessionCreated{.session_id = "nobody_listens"}));
  EXPECT_NO_THROW(Bus::instance().publish(StreamDelta{.session_id = "s", .text = "t"}));
  EXPECT_NO_THROW(Bus::instance().publish(ToolCallCompleted{.session_id = "s", .tool_id = "t", .tool_name = "n", .success = true}));
}

// 7. 测试 SessionCreated 事件字段
TEST_F(BusTest, SessionCreatedEvent) {
  SessionCreated captured;
  track(Bus::instance().subscribe<SessionCreated>([&](const SessionCreated& e) {
    captured = e;
  }));

  Bus::instance().publish(SessionCreated{.session_id = "sess_abc_123"});

  EXPECT_EQ(captured.session_id, "sess_abc_123");
}

// 8. 测试工具调用事件
TEST_F(BusTest, ToolCallEvents) {
  ToolCallStarted captured_start;
  ToolCallCompleted captured_complete;

  track(Bus::instance().subscribe<ToolCallStarted>([&](const ToolCallStarted& e) {
    captured_start = e;
  }));

  track(Bus::instance().subscribe<ToolCallCompleted>([&](const ToolCallCompleted& e) {
    captured_complete = e;
  }));

  Bus::instance().publish(ToolCallStarted{
      .session_id = "sess_tool",
      .tool_id = "tc_001",
      .tool_name = "bash",
  });

  EXPECT_EQ(captured_start.session_id, "sess_tool");
  EXPECT_EQ(captured_start.tool_id, "tc_001");
  EXPECT_EQ(captured_start.tool_name, "bash");

  Bus::instance().publish(ToolCallCompleted{
      .session_id = "sess_tool",
      .tool_id = "tc_001",
      .tool_name = "bash",
      .success = true,
  });

  EXPECT_EQ(captured_complete.session_id, "sess_tool");
  EXPECT_EQ(captured_complete.tool_id, "tc_001");
  EXPECT_EQ(captured_complete.tool_name, "bash");
  EXPECT_TRUE(captured_complete.success);

  // 测试失败的工具调用
  Bus::instance().publish(ToolCallCompleted{
      .session_id = "sess_tool",
      .tool_id = "tc_002",
      .tool_name = "read",
      .success = false,
  });

  EXPECT_EQ(captured_complete.tool_id, "tc_002");
  EXPECT_EQ(captured_complete.tool_name, "read");
  EXPECT_FALSE(captured_complete.success);
}

// 9. 测试 MCP 工具变更事件
TEST_F(BusTest, McpToolsChangedEvent) {
  std::vector<std::string> received_servers;

  track(Bus::instance().subscribe<McpToolsChanged>([&](const McpToolsChanged& e) {
    received_servers.push_back(e.server_name);
  }));

  Bus::instance().publish(McpToolsChanged{.server_name = "mcp-server-filesystem"});
  Bus::instance().publish(McpToolsChanged{.server_name = "mcp-server-github"});

  ASSERT_EQ(received_servers.size(), 2);
  EXPECT_EQ(received_servers[0], "mcp-server-filesystem");
  EXPECT_EQ(received_servers[1], "mcp-server-github");
}
