#include "auth/qwen_oauth.hpp"

#include <spdlog/spdlog.h>

#include <iostream>

#include "agent/agent.hpp"

using namespace agent;
using namespace agent::auth;

int main() {
  // 启用 debug 日志
  spdlog::set_level(spdlog::level::debug);

  std::cout << "=== Qwen OAuth API Test ===\n" << std::endl;

  // 1. 检查 OAuth Token
  auto& auth = qwen_portal_auth();
  auto token = auth.load_token();

  if (!token) {
    std::cerr << "Error: No Qwen OAuth token found." << std::endl;
    std::cerr << "Please login using: qwen auth login" << std::endl;
    std::cerr << "Or run this program with OPENAI_API_KEY set." << std::endl;
    return 1;
  }

  std::cout << "Token loaded successfully:" << std::endl;
  std::cout << "  Provider: " << token->provider << std::endl;
  std::cout << "  Access Token: " << token->access_token.substr(0, 20) << "..." << std::endl;
  std::cout << "  Is Expired: " << (token->is_expired() ? "Yes" : "No") << std::endl;
  std::cout << "  Needs Refresh: " << (token->needs_refresh() ? "Yes" : "No") << std::endl;
  std::cout << std::endl;

  // 2. 设置配置
  Config config;
  config.providers["openai"] = ProviderConfig{
      "openai",
      "qwen-oauth",              // 使用 OAuth 占位符
      "https://portal.qwen.ai",  // Qwen Portal base URL
      std::nullopt,
      {},
  };
  config.default_model = "coder-model";  // Portal 支持的模型别名

  // 3. 初始化
  asio::io_context io_ctx;
  agent::init();

  // 4. 创建 Session
  auto session = Session::create(io_ctx, config, AgentType::Build);

  // 5. 设置回调
  std::string response_text;
  bool completed = false;
  bool has_error = false;
  std::string error_msg;

  session->on_stream([&response_text](const std::string& text) {
    std::cout << text << std::flush;
    response_text += text;
  });

  session->on_error([&](const std::string& error) {
    has_error = true;
    error_msg = error;
    std::cerr << "\n[Error] " << error << std::endl;
  });

  session->on_complete([&](FinishReason reason) {
    completed = true;
    std::cout << "\n\n[Complete] Finish reason: ";
    switch (reason) {
      case FinishReason::Stop:
        std::cout << "Stop";
        break;
      case FinishReason::ToolCalls:
        std::cout << "ToolCalls";
        break;
      case FinishReason::Length:
        std::cout << "Length";
        break;
      case FinishReason::Error:
        std::cout << "Error";
        break;
    }
    std::cout << std::endl;
  });

  // 6. 发送测试消息
  std::cout << "Sending test prompt to Qwen API..." << std::endl;
  std::cout << "Model: " << config.default_model << std::endl;
  std::cout << "Base URL: https://portal.qwen.ai" << std::endl;
  std::cout << "\n--- Response ---\n" << std::endl;

  session->prompt("Say 'Hello from Qwen!' in exactly 5 words.");

  // 7. 运行 IO context
  std::thread io_thread([&io_ctx]() {
    auto work = asio::make_work_guard(io_ctx);
    io_ctx.run();
  });

  // 等待完成（最多 30 秒）
  auto start = std::chrono::steady_clock::now();
  while (!completed && !has_error) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto elapsed = std::chrono::steady_clock::now() - start;
    if (elapsed > std::chrono::seconds(30)) {
      std::cerr << "\n[Timeout] No response received within 30 seconds." << std::endl;
      break;
    }
  }

  // 8. 清理
  session->cancel();
  io_ctx.stop();
  if (io_thread.joinable()) io_thread.join();

  std::cout << "\n=== Test Complete ===" << std::endl;

  if (has_error) {
    return 1;
  }

  if (response_text.empty()) {
    std::cerr << "Warning: No response text received." << std::endl;
    return 1;
  }

  return 0;
}
