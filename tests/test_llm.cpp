#include <gtest/gtest.h>

#include "llm/anthropic.hpp"
#include "llm/openai.hpp"
#include "llm/provider.hpp"
#include "tool/tool.hpp"

using namespace agent;
using namespace agent::llm;

TEST(LlmTest, ProviderFactory) {
  auto& factory = ProviderFactory::instance();

  // Without config, should return nullptr
  ProviderConfig empty_config;
  asio::io_context io_ctx;

  auto provider = factory.create("anthropic", empty_config, io_ctx);
  EXPECT_NE(provider, nullptr);
}

TEST(LlmTest, ProviderFactoryOpenAI) {
  auto& factory = ProviderFactory::instance();

  ProviderConfig config;
  config.api_key = "test-key";
  asio::io_context io_ctx;

  auto provider = factory.create("openai", config, io_ctx);
  EXPECT_NE(provider, nullptr);
  EXPECT_EQ(provider->name(), "openai");
}

TEST(LlmTest, ProviderFactoryUnknown) {
  auto& factory = ProviderFactory::instance();

  ProviderConfig config;
  asio::io_context io_ctx;

  auto provider = factory.create("unknown_provider", config, io_ctx);
  EXPECT_EQ(provider, nullptr);
}

TEST(LlmTest, AnthropicModels) {
  asio::io_context io_ctx;
  ProviderConfig config;
  config.api_key = "test-key";

  AnthropicProvider provider(config, io_ctx);

  auto models = provider.models();
  EXPECT_GT(models.size(), 0);

  // Check for Claude Sonnet
  bool has_sonnet = false;
  for (const auto& model : models) {
    if (model.id.find("sonnet") != std::string::npos) {
      has_sonnet = true;
      break;
    }
  }
  EXPECT_TRUE(has_sonnet);
}

TEST(LlmTest, OpenAIModels) {
  asio::io_context io_ctx;
  ProviderConfig config;
  config.api_key = "test-key";

  OpenAIProvider provider(config, io_ctx);

  auto models = provider.models();
  EXPECT_GT(models.size(), 0);

  // Check for GPT-4o
  bool has_gpt4o = false;
  for (const auto& model : models) {
    if (model.id == "gpt-4o") {
      has_gpt4o = true;
      EXPECT_EQ(model.provider, "openai");
      EXPECT_TRUE(model.supports_vision);
      EXPECT_TRUE(model.supports_tools);
      break;
    }
  }
  EXPECT_TRUE(has_gpt4o);
}

TEST(LlmTest, OpenAIGetModel) {
  asio::io_context io_ctx;
  ProviderConfig config;
  config.api_key = "test-key";

  OpenAIProvider provider(config, io_ctx);

  auto model = provider.get_model("gpt-4o");
  ASSERT_TRUE(model.has_value());
  EXPECT_EQ(model->id, "gpt-4o");
  EXPECT_EQ(model->provider, "openai");

  auto unknown = provider.get_model("nonexistent-model");
  EXPECT_FALSE(unknown.has_value());
}

TEST(LlmTest, RequestFormat) {
  LlmRequest request;
  request.model = "claude-sonnet-4-20250514";
  request.system_prompt = "You are a helpful assistant.";
  request.messages.push_back(Message::user("Hello"));

  auto anthropic_json = request.to_anthropic_format();

  EXPECT_EQ(anthropic_json["model"], "claude-sonnet-4-20250514");
  EXPECT_EQ(anthropic_json["system"], "You are a helpful assistant.");
  EXPECT_TRUE(anthropic_json.contains("messages"));
}

TEST(LlmTest, OpenAIRequestFormat) {
  LlmRequest request;
  request.model = "gpt-4o";
  request.system_prompt = "You are a helpful assistant.";
  request.max_tokens = 4096;
  request.temperature = 0.7;
  request.messages.push_back(Message::user("Hello"));

  auto openai_json = request.to_openai_format();

  EXPECT_EQ(openai_json["model"], "gpt-4o");
  EXPECT_EQ(openai_json["max_tokens"], 4096);
  EXPECT_DOUBLE_EQ(openai_json["temperature"], 0.7);
  EXPECT_TRUE(openai_json.contains("messages"));

  // System prompt should be a separate message in OpenAI format
  auto& msgs = openai_json["messages"];
  EXPECT_GE(msgs.size(), 2);  // system + user
  EXPECT_EQ(msgs[0]["role"], "system");
  EXPECT_EQ(msgs[0]["content"], "You are a helpful assistant.");
  EXPECT_EQ(msgs[1]["role"], "user");
}

// ============================================================
// Helper: mock tool for request format tests
// ============================================================
class MockTool : public agent::SimpleTool {
 public:
  MockTool() : SimpleTool("mock_tool", "A mock tool for testing") {}

  std::vector<agent::ParameterSchema> parameters() const override {
    return {
        {"query", "string", "The search query", true, std::nullopt, std::nullopt},
        {"limit", "number", "Max results", false, json(10), std::nullopt},
    };
  }

  std::future<agent::ToolResult> execute(const json& /*args*/, const agent::ToolContext& /*ctx*/) override {
    std::promise<agent::ToolResult> p;
    p.set_value(agent::ToolResult::success("ok"));
    return p.get_future();
  }
};

// ============================================================
// LlmRequestTest — Anthropic format
// ============================================================
TEST(LlmRequestTest, AnthropicFormat) {
  LlmRequest request;
  request.model = "claude-sonnet-4-20250514";
  request.system_prompt = "You are a coding assistant.";
  request.max_tokens = 4096;
  request.temperature = 0.5;
  request.stop_sequences = std::vector<std::string>{"END"};

  // Add several messages: user, assistant, user with tool result
  request.messages.push_back(Message::user("Hello"));
  request.messages.push_back(Message::assistant("Hi there!"));

  // User message with tool result
  Message tool_msg(Role::User, "");
  tool_msg.add_tool_result("call_123", "mock_tool", "result output", false);
  request.messages.push_back(tool_msg);

  // Add a tool
  auto tool = std::make_shared<MockTool>();
  request.tools.push_back(tool);

  auto j = request.to_anthropic_format();

  // Top-level fields
  EXPECT_EQ(j["model"], "claude-sonnet-4-20250514");
  EXPECT_EQ(j["system"], "You are a coding assistant.");
  EXPECT_EQ(j["max_tokens"], 4096);
  EXPECT_DOUBLE_EQ(j["temperature"].get<double>(), 0.5);
  ASSERT_TRUE(j.contains("stop_sequences"));
  EXPECT_EQ(j["stop_sequences"][0], "END");

  // Messages array
  ASSERT_TRUE(j.contains("messages"));
  auto& msgs = j["messages"];
  EXPECT_EQ(msgs.size(), 3);

  // First message: user
  EXPECT_EQ(msgs[0]["role"], "user");
  // Content can be a string (single text part optimization)
  EXPECT_EQ(msgs[0]["content"], "Hello");

  // Second message: assistant
  EXPECT_EQ(msgs[1]["role"], "assistant");
  EXPECT_EQ(msgs[1]["content"], "Hi there!");

  // Third message: user with tool_result
  EXPECT_EQ(msgs[2]["role"], "user");
  auto& content = msgs[2]["content"];
  ASSERT_TRUE(content.is_array());
  ASSERT_GE(content.size(), 1);
  EXPECT_EQ(content[0]["type"], "tool_result");
  EXPECT_EQ(content[0]["tool_use_id"], "call_123");
  EXPECT_EQ(content[0]["content"], "result output");
  EXPECT_EQ(content[0]["is_error"], false);

  // Tools array
  ASSERT_TRUE(j.contains("tools"));
  auto& tools_json = j["tools"];
  EXPECT_EQ(tools_json.size(), 1);
  EXPECT_EQ(tools_json[0]["name"], "mock_tool");
  EXPECT_EQ(tools_json[0]["description"], "A mock tool for testing");
  ASSERT_TRUE(tools_json[0].contains("input_schema"));
  auto& schema = tools_json[0]["input_schema"];
  EXPECT_EQ(schema["type"], "object");
  EXPECT_TRUE(schema["properties"].contains("query"));
  EXPECT_TRUE(schema["properties"].contains("limit"));
}

TEST(LlmRequestTest, AnthropicFormatNoOptionals) {
  // Request without optional fields
  LlmRequest request;
  request.model = "claude-3-5-haiku-20241022";
  request.messages.push_back(Message::user("test"));

  auto j = request.to_anthropic_format();

  EXPECT_EQ(j["model"], "claude-3-5-haiku-20241022");
  EXPECT_EQ(j["max_tokens"], 8192);  // default when not set
  EXPECT_FALSE(j.contains("system"));
  EXPECT_FALSE(j.contains("temperature"));
  EXPECT_FALSE(j.contains("stop_sequences"));
  EXPECT_FALSE(j.contains("tools"));
}

TEST(LlmRequestTest, AnthropicFormatToolCallMessage) {
  // Assistant message with a tool_use block
  LlmRequest request;
  request.model = "claude-sonnet-4-20250514";

  request.messages.push_back(Message::user("Search for cats"));

  Message assistant_msg(Role::Assistant, "");
  assistant_msg.add_text("Let me search for that.");
  assistant_msg.add_tool_call("tc_001", "mock_tool", json{{"query", "cats"}});
  request.messages.push_back(assistant_msg);

  auto j = request.to_anthropic_format();

  auto& msgs = j["messages"];
  ASSERT_EQ(msgs.size(), 2);

  // The assistant message has mixed content → should be an array
  auto& content = msgs[1]["content"];
  ASSERT_TRUE(content.is_array());
  EXPECT_EQ(content.size(), 2);
  EXPECT_EQ(content[0]["type"], "text");
  EXPECT_EQ(content[0]["text"], "Let me search for that.");
  EXPECT_EQ(content[1]["type"], "tool_use");
  EXPECT_EQ(content[1]["id"], "tc_001");
  EXPECT_EQ(content[1]["name"], "mock_tool");
  EXPECT_EQ(content[1]["input"]["query"], "cats");
}

// ============================================================
// LlmRequestTest — OpenAI format
// ============================================================
TEST(LlmRequestTest, OpenAIFormat) {
  LlmRequest request;
  request.model = "gpt-4o";
  request.system_prompt = "You are a coding assistant.";
  request.max_tokens = 2048;
  request.temperature = 0.3;
  request.stop_sequences = std::vector<std::string>{"STOP", "END"};

  request.messages.push_back(Message::user("Hello"));

  auto tool = std::make_shared<MockTool>();
  request.tools.push_back(tool);

  auto j = request.to_openai_format();

  // Top-level fields
  EXPECT_EQ(j["model"], "gpt-4o");
  EXPECT_EQ(j["max_tokens"], 2048);
  EXPECT_DOUBLE_EQ(j["temperature"].get<double>(), 0.3);
  ASSERT_TRUE(j.contains("stop"));
  EXPECT_EQ(j["stop"].size(), 2);
  EXPECT_EQ(j["stop"][0], "STOP");

  // Messages: system + user
  auto& msgs = j["messages"];
  ASSERT_GE(msgs.size(), 2);
  EXPECT_EQ(msgs[0]["role"], "system");
  EXPECT_EQ(msgs[0]["content"], "You are a coding assistant.");
  EXPECT_EQ(msgs[1]["role"], "user");

  // Tools in OpenAI format (wrapped in { type: "function", function: {...} })
  ASSERT_TRUE(j.contains("tools"));
  auto& tools_json = j["tools"];
  EXPECT_EQ(tools_json.size(), 1);
  EXPECT_EQ(tools_json[0]["type"], "function");
  EXPECT_EQ(tools_json[0]["function"]["name"], "mock_tool");
  EXPECT_EQ(tools_json[0]["function"]["description"], "A mock tool for testing");
  ASSERT_TRUE(tools_json[0]["function"].contains("parameters"));
}

TEST(LlmRequestTest, OpenAIFormatToolResultMessage) {
  // OpenAI encodes tool results as role="tool" messages
  LlmRequest request;
  request.model = "gpt-4o";
  request.messages.push_back(Message::user("search"));

  Message assistant_msg(Role::Assistant, "");
  assistant_msg.add_tool_call("call_abc", "mock_tool", json{{"query", "dogs"}});
  request.messages.push_back(assistant_msg);

  Message tool_result_msg(Role::User, "");
  tool_result_msg.add_tool_result("call_abc", "mock_tool", "found 10 results");
  request.messages.push_back(tool_result_msg);

  auto j = request.to_openai_format();

  auto& msgs = j["messages"];
  // Should have: user("search"), assistant(tool_call), tool(result)
  ASSERT_GE(msgs.size(), 3);

  // Find the tool result message
  bool found_tool = false;
  for (const auto& msg : msgs) {
    if (msg["role"] == "tool") {
      found_tool = true;
      EXPECT_EQ(msg["tool_call_id"], "call_abc");
      EXPECT_EQ(msg["content"], "found 10 results");
    }
  }
  EXPECT_TRUE(found_tool);
}

TEST(LlmRequestTest, OpenAIFormatNoOptionals) {
  LlmRequest request;
  request.model = "gpt-4o-mini";
  request.messages.push_back(Message::user("hi"));

  auto j = request.to_openai_format();

  EXPECT_EQ(j["model"], "gpt-4o-mini");
  EXPECT_FALSE(j.contains("max_tokens"));   // not set
  EXPECT_FALSE(j.contains("temperature"));  // not set
  EXPECT_FALSE(j.contains("stop"));         // not set
  EXPECT_FALSE(j.contains("tools"));        // no tools
  // no system_prompt → messages only has user
  EXPECT_EQ(j["messages"].size(), 1);
  EXPECT_EQ(j["messages"][0]["role"], "user");
}

// ============================================================
// StreamEventTest — variant construction and visitation
// ============================================================
TEST(StreamEventTest, TextDelta) {
  StreamEvent event = TextDelta{"Hello, world!"};

  ASSERT_TRUE(std::holds_alternative<TextDelta>(event));
  auto& td = std::get<TextDelta>(event);
  EXPECT_EQ(td.text, "Hello, world!");
}

TEST(StreamEventTest, TextDeltaEmpty) {
  StreamEvent event = TextDelta{""};
  auto& td = std::get<TextDelta>(event);
  EXPECT_TRUE(td.text.empty());
}

TEST(StreamEventTest, ToolCallDelta) {
  StreamEvent event = ToolCallDelta{"call_001", "bash", "{\"command\":"};

  ASSERT_TRUE(std::holds_alternative<ToolCallDelta>(event));
  auto& tcd = std::get<ToolCallDelta>(event);
  EXPECT_EQ(tcd.id, "call_001");
  EXPECT_EQ(tcd.name, "bash");
  EXPECT_EQ(tcd.arguments_delta, "{\"command\":");
}

TEST(StreamEventTest, ToolCallDeltaEmptyArgs) {
  // Initial delta often has empty arguments_delta
  StreamEvent event = ToolCallDelta{"tc_abc", "read_file", ""};
  auto& tcd = std::get<ToolCallDelta>(event);
  EXPECT_EQ(tcd.id, "tc_abc");
  EXPECT_EQ(tcd.name, "read_file");
  EXPECT_TRUE(tcd.arguments_delta.empty());
}

TEST(StreamEventTest, ToolCallComplete) {
  json args = {{"path", "/tmp/test.txt"}, {"line", 42}};
  StreamEvent event = ToolCallComplete{"call_002", "read_file", args};

  ASSERT_TRUE(std::holds_alternative<ToolCallComplete>(event));
  auto& tcc = std::get<ToolCallComplete>(event);
  EXPECT_EQ(tcc.id, "call_002");
  EXPECT_EQ(tcc.name, "read_file");
  EXPECT_EQ(tcc.arguments["path"], "/tmp/test.txt");
  EXPECT_EQ(tcc.arguments["line"], 42);
}

TEST(StreamEventTest, ToolCallCompleteEmptyArgs) {
  StreamEvent event = ToolCallComplete{"call_003", "list_dir", json::object()};
  auto& tcc = std::get<ToolCallComplete>(event);
  EXPECT_TRUE(tcc.arguments.empty());
}

TEST(StreamEventTest, UsageInfo) {
  FinishStep finish;
  finish.reason = FinishReason::Stop;
  finish.usage.input_tokens = 150;
  finish.usage.output_tokens = 50;
  finish.usage.cache_read_tokens = 100;
  finish.usage.cache_write_tokens = 0;

  StreamEvent event = finish;

  ASSERT_TRUE(std::holds_alternative<FinishStep>(event));
  auto& fs = std::get<FinishStep>(event);
  EXPECT_EQ(fs.reason, FinishReason::Stop);
  EXPECT_EQ(fs.usage.input_tokens, 150);
  EXPECT_EQ(fs.usage.output_tokens, 50);
  EXPECT_EQ(fs.usage.cache_read_tokens, 100);
  EXPECT_EQ(fs.usage.cache_write_tokens, 0);
  EXPECT_EQ(fs.usage.total(), 200);
}

TEST(StreamEventTest, UsageToolCalls) {
  FinishStep finish;
  finish.reason = FinishReason::ToolCalls;
  finish.usage.input_tokens = 500;
  finish.usage.output_tokens = 200;

  StreamEvent event = finish;
  auto& fs = std::get<FinishStep>(event);
  EXPECT_EQ(fs.reason, FinishReason::ToolCalls);
  EXPECT_EQ(fs.usage.total(), 700);
}

TEST(StreamEventTest, UsageLength) {
  FinishStep finish;
  finish.reason = FinishReason::Length;
  finish.usage.output_tokens = 8192;

  StreamEvent event = finish;
  auto& fs = std::get<FinishStep>(event);
  EXPECT_EQ(fs.reason, FinishReason::Length);
}

TEST(StreamEventTest, StreamError) {
  StreamError err;
  err.message = "rate limit exceeded";
  err.retryable = true;

  StreamEvent event = err;

  ASSERT_TRUE(std::holds_alternative<StreamError>(event));
  auto& se = std::get<StreamError>(event);
  EXPECT_EQ(se.message, "rate limit exceeded");
  EXPECT_TRUE(se.retryable);
}

TEST(StreamEventTest, StreamErrorNonRetryable) {
  StreamEvent event = StreamError{"invalid api key", false};
  auto& se = std::get<StreamError>(event);
  EXPECT_EQ(se.message, "invalid api key");
  EXPECT_FALSE(se.retryable);
}

TEST(StreamEventTest, VariantVisitor) {
  // Verify visitor pattern works across all variants
  std::vector<StreamEvent> events = {
      TextDelta{"hello"},
      ToolCallDelta{"id1", "tool1", "args"},
      ToolCallComplete{"id2", "tool2", json::object()},
      FinishStep{FinishReason::Stop, {}},
      StreamError{"oops", false},
  };

  int text_count = 0, delta_count = 0, complete_count = 0, finish_count = 0, error_count = 0;

  for (const auto& event : events) {
    std::visit(
        [&](auto&& arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, TextDelta>)
            text_count++;
          else if constexpr (std::is_same_v<T, ToolCallDelta>)
            delta_count++;
          else if constexpr (std::is_same_v<T, ToolCallComplete>)
            complete_count++;
          else if constexpr (std::is_same_v<T, FinishStep>)
            finish_count++;
          else if constexpr (std::is_same_v<T, StreamError>)
            error_count++;
        },
        event);
  }

  EXPECT_EQ(text_count, 1);
  EXPECT_EQ(delta_count, 1);
  EXPECT_EQ(complete_count, 1);
  EXPECT_EQ(finish_count, 1);
  EXPECT_EQ(error_count, 1);
}

// ============================================================
// ModelInfoTest — provider model metadata
// ============================================================
TEST(ModelInfoTest, GetAnthropicModel) {
  asio::io_context io_ctx;
  ProviderConfig config;
  config.api_key = "test-key";

  AnthropicProvider provider(config, io_ctx);

  // Claude Sonnet 4
  auto model = provider.get_model("claude-sonnet-4-20250514");
  ASSERT_TRUE(model.has_value());
  EXPECT_EQ(model->id, "claude-sonnet-4-20250514");
  EXPECT_EQ(model->provider, "anthropic");
  EXPECT_EQ(model->context_window, 200000);
  EXPECT_EQ(model->max_output_tokens, 64000);
  EXPECT_TRUE(model->supports_vision);
  EXPECT_TRUE(model->supports_tools);

  // Claude Opus 4
  auto opus = provider.get_model("claude-opus-4-20250514");
  ASSERT_TRUE(opus.has_value());
  EXPECT_EQ(opus->provider, "anthropic");
  EXPECT_EQ(opus->context_window, 200000);
  EXPECT_EQ(opus->max_output_tokens, 32000);

  // Claude 3.5 Haiku
  auto haiku = provider.get_model("claude-3-5-haiku-20241022");
  ASSERT_TRUE(haiku.has_value());
  EXPECT_EQ(haiku->max_output_tokens, 8192);

  // Non-existent model
  auto unknown = provider.get_model("nonexistent");
  EXPECT_FALSE(unknown.has_value());
}

TEST(ModelInfoTest, GetOpenAIModel) {
  asio::io_context io_ctx;
  ProviderConfig config;
  config.api_key = "test-key";

  OpenAIProvider provider(config, io_ctx);

  // GPT-4o
  auto model = provider.get_model("gpt-4o");
  ASSERT_TRUE(model.has_value());
  EXPECT_EQ(model->id, "gpt-4o");
  EXPECT_EQ(model->provider, "openai");
  EXPECT_EQ(model->context_window, 128000);
  EXPECT_EQ(model->max_output_tokens, 16384);
  EXPECT_TRUE(model->supports_vision);
  EXPECT_TRUE(model->supports_tools);

  // GPT-4.1
  auto gpt41 = provider.get_model("gpt-4.1");
  ASSERT_TRUE(gpt41.has_value());
  EXPECT_EQ(gpt41->provider, "openai");
  EXPECT_EQ(gpt41->context_window, 1047576);
  EXPECT_EQ(gpt41->max_output_tokens, 32768);
  EXPECT_TRUE(gpt41->supports_vision);

  // o3
  auto o3 = provider.get_model("o3");
  ASSERT_TRUE(o3.has_value());
  EXPECT_EQ(o3->context_window, 200000);
  EXPECT_EQ(o3->max_output_tokens, 100000);
  EXPECT_TRUE(o3->supports_vision);
  EXPECT_TRUE(o3->supports_tools);

  // o3-mini (no vision)
  auto o3mini = provider.get_model("o3-mini");
  ASSERT_TRUE(o3mini.has_value());
  EXPECT_FALSE(o3mini->supports_vision);
  EXPECT_TRUE(o3mini->supports_tools);

  // Non-existent model
  auto unknown = provider.get_model("nonexistent");
  EXPECT_FALSE(unknown.has_value());
}

TEST(ModelInfoTest, AnthropicModelCount) {
  asio::io_context io_ctx;
  ProviderConfig config;
  config.api_key = "test-key";

  AnthropicProvider provider(config, io_ctx);
  auto models = provider.models();

  // Should have at least 5 models (opus4, sonnet4, 3.5-sonnet, 3.5-haiku, 3-opus)
  EXPECT_GE(models.size(), 5);

  // All models should be from anthropic
  for (const auto& m : models) {
    EXPECT_EQ(m.provider, "anthropic");
    EXPECT_GT(m.context_window, 0);
    EXPECT_GT(m.max_output_tokens, 0);
  }
}

TEST(ModelInfoTest, OpenAIModelCount) {
  asio::io_context io_ctx;
  ProviderConfig config;
  config.api_key = "test-key";

  OpenAIProvider provider(config, io_ctx);
  auto models = provider.models();

  // Should have at least 8 models
  EXPECT_GE(models.size(), 8);

  // All models should be from openai
  for (const auto& m : models) {
    EXPECT_EQ(m.provider, "openai");
    EXPECT_GT(m.context_window, 0);
    EXPECT_GT(m.max_output_tokens, 0);
  }
}
