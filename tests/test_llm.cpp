#include <gtest/gtest.h>

#include "llm/anthropic.hpp"
#include "llm/openai.hpp"
#include "llm/provider.hpp"

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
