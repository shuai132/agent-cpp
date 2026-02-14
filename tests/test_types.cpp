#include <gtest/gtest.h>

#include <string>

#include "core/types.hpp"

using namespace agent;

// --- ResultTest ---

TEST(ResultTest, Success) {
  auto result = Result<int>::success(42);

  EXPECT_TRUE(result.ok());
  EXPECT_FALSE(result.failed());
  ASSERT_TRUE(result.value.has_value());
  EXPECT_EQ(*result.value, 42);
  EXPECT_FALSE(result.error.has_value());
}

TEST(ResultTest, Failure) {
  auto result = Result<int>::failure("something went wrong");

  EXPECT_FALSE(result.ok());
  EXPECT_TRUE(result.failed());
  EXPECT_FALSE(result.value.has_value());
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(*result.error, "something went wrong");
}

TEST(ResultTest, DefaultState) {
  Result<std::string> result;

  EXPECT_FALSE(result.ok());
  EXPECT_FALSE(result.failed());
  EXPECT_FALSE(result.value.has_value());
  EXPECT_FALSE(result.error.has_value());
}

// --- TokenUsageTest ---

TEST(TokenUsageTest, Total) {
  TokenUsage usage;
  usage.input_tokens = 100;
  usage.output_tokens = 50;
  usage.cache_read_tokens = 30;
  usage.cache_write_tokens = 20;

  // total() = input_tokens + output_tokens
  EXPECT_EQ(usage.total(), 150);
}

TEST(TokenUsageTest, PlusEquals) {
  TokenUsage a;
  a.input_tokens = 100;
  a.output_tokens = 50;
  a.cache_read_tokens = 30;
  a.cache_write_tokens = 20;

  TokenUsage b;
  b.input_tokens = 200;
  b.output_tokens = 100;
  b.cache_read_tokens = 10;
  b.cache_write_tokens = 5;

  a += b;

  EXPECT_EQ(a.input_tokens, 300);
  EXPECT_EQ(a.output_tokens, 150);
  EXPECT_EQ(a.cache_read_tokens, 40);
  EXPECT_EQ(a.cache_write_tokens, 25);
  EXPECT_EQ(a.total(), 450);
}

// --- FinishReasonTest ---

TEST(FinishReasonTest, ToString) {
  EXPECT_EQ(to_string(FinishReason::Stop), "stop");
  EXPECT_EQ(to_string(FinishReason::ToolCalls), "tool_calls");
  EXPECT_EQ(to_string(FinishReason::Length), "length");
  EXPECT_EQ(to_string(FinishReason::Error), "error");
  EXPECT_EQ(to_string(FinishReason::Cancelled), "cancelled");
}

TEST(FinishReasonTest, FromString) {
  // 标准字符串
  EXPECT_EQ(finish_reason_from_string("stop"), FinishReason::Stop);
  EXPECT_EQ(finish_reason_from_string("tool_calls"), FinishReason::ToolCalls);
  EXPECT_EQ(finish_reason_from_string("length"), FinishReason::Length);
  EXPECT_EQ(finish_reason_from_string("error"), FinishReason::Error);
  EXPECT_EQ(finish_reason_from_string("cancelled"), FinishReason::Cancelled);

  // 别名
  EXPECT_EQ(finish_reason_from_string("end_turn"), FinishReason::Stop);
  EXPECT_EQ(finish_reason_from_string("tool_use"), FinishReason::ToolCalls);
  EXPECT_EQ(finish_reason_from_string("max_tokens"), FinishReason::Length);

  // 未知字符串默认为 Stop
  EXPECT_EQ(finish_reason_from_string("unknown_value"), FinishReason::Stop);
}

// --- AgentTypeTest ---

TEST(AgentTypeTest, ToString) {
  EXPECT_EQ(to_string(AgentType::Build), "build");
  EXPECT_EQ(to_string(AgentType::Explore), "explore");
  EXPECT_EQ(to_string(AgentType::General), "general");
  EXPECT_EQ(to_string(AgentType::Plan), "plan");
  EXPECT_EQ(to_string(AgentType::Compaction), "compaction");
}

TEST(AgentTypeTest, FromString) {
  EXPECT_EQ(agent_type_from_string("build"), AgentType::Build);
  EXPECT_EQ(agent_type_from_string("explore"), AgentType::Explore);
  EXPECT_EQ(agent_type_from_string("general"), AgentType::General);
  EXPECT_EQ(agent_type_from_string("plan"), AgentType::Plan);
  EXPECT_EQ(agent_type_from_string("compaction"), AgentType::Compaction);

  // 未知字符串默认为 Build
  EXPECT_EQ(agent_type_from_string("nonexistent"), AgentType::Build);
}

// --- SanitizeUtf8Test ---

TEST(SanitizeUtf8Test, ValidUtf8) {
  // ASCII
  EXPECT_EQ(sanitize_utf8("hello world"), "hello world");

  // 多字节 UTF-8：中文
  std::string chinese = "你好世界";
  EXPECT_EQ(sanitize_utf8(chinese), chinese);

  // 4 字节 UTF-8：emoji
  std::string emoji = "😀";
  EXPECT_EQ(sanitize_utf8(emoji), emoji);

  // 空字符串
  EXPECT_EQ(sanitize_utf8(""), "");
}

TEST(SanitizeUtf8Test, InvalidBytes) {
  // 单独的无效前导字节 0xFF
  std::string invalid_byte("\xff", 1);
  std::string result = sanitize_utf8(invalid_byte);
  // U+FFFD 的 UTF-8 编码为 \xEF\xBF\xBD
  EXPECT_EQ(result, "\xEF\xBF\xBD");

  // 不完整的 2 字节序列：0xC2 后面没有后续字节
  std::string incomplete_2byte("\xC2", 1);
  EXPECT_EQ(sanitize_utf8(incomplete_2byte), "\xEF\xBF\xBD");

  // 混合有效和无效字节
  std::string mixed = std::string("hello") + std::string("\xFE", 1) + std::string("world");
  result = sanitize_utf8(mixed);
  EXPECT_EQ(result, "hello\xEF\xBF\xBDworld");
}
