#include <termios.h>
#include <unistd.h>

#include <asio.hpp>
#include <asio/steady_timer.hpp>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <future>
#include <sstream>
#include <thread>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/loop.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <iostream>
#include <string>

#include "agent/agent.hpp"
#include "core/version.hpp"
#include "llm/qwen_oauth.hpp"
#include "tui_callbacks.h"
#include "tui_components.h"
#include "tui_event_handler.h"
#include "tui_render.h"
#include "tui_state.h"

using namespace agent;
using namespace agent_cli;
using namespace ftxui;

// Simple HTTP server to handle OAuth callback
class OAuthCallbackServer {
public:
    OAuthCallbackServer(asio::io_context& io_ctx, unsigned short port = 8080)
        : acceptor_(io_ctx, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
          timer_(io_ctx) {
        start_accept();
    }

    void start_accept() {
        auto new_socket = std::make_shared<asio::ip::tcp::socket>(acceptor_.get_executor());
        acceptor_.async_accept(*new_socket,
            [this, new_socket](std::error_code ec) {
                if (!ec) {
                    start_read_request(new_socket);
                }
                start_accept(); // Accept next connection
            });
    }

    void start_read_request(std::shared_ptr<asio::ip::tcp::socket> socket) {
        auto buffer = std::make_shared<std::string>();
        asio::async_read_until(*socket, asio::dynamic_buffer(*buffer), "\r\n\r\n",
            [this, socket, buffer](std::error_code ec, std::size_t /*bytes_transferred*/) {
                if (!ec) {
                    // Extract authorization code from the request
                    std::string code = extract_code_from_request(*buffer);
                    
                    // Send response to browser
                    std::string response = 
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/html\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                        "<html><body>"
                        "<h1>Authentication Successful!</h1>"
                        "<p>You can now close this window and return to the application.</p>"
                        "</body></html>";
                        
                    asio::async_write(*socket, asio::buffer(response),
                        [this, code](std::error_code ec, std::size_t /*bytes_transferred*/) {
                            if (!ec && !code.empty()) {
                                auth_code_ = code;
                                // Stop accepting new connections after getting the code
                                acceptor_.close();
                            }
                        });
                }
            });
    }

    std::string extract_code_from_request(const std::string& request) {
        // Find the position of "code="
        std::size_t pos = request.find("code=");
        if (pos != std::string::npos) {
            pos += 5; // Move past "code="
            std::size_t end_pos = request.find_first_of("& \r\n", pos);
            if (end_pos == std::string::npos) {
                end_pos = request.length();
            }
            return request.substr(pos, end_pos - pos);
        }
        return "";
    }

    std::string wait_for_code(int timeout_seconds = 120) {
        auto start_time = std::chrono::steady_clock::now();
        
        while (auth_code_.empty()) {
            // Check for timeout
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - start_time).count();
                
            if (elapsed >= timeout_seconds) {
                break;
            }
            
            // Small delay to prevent busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        return auth_code_;
    }

private:
    asio::ip::tcp::acceptor acceptor_;
    asio::steady_timer timer_;
    std::string auth_code_;
};

// Helper function to perform OAuth flow with local server
std::string perform_qwen_oauth_with_local_server() {
    const char* client_id = std::getenv("QWEN_CLIENT_ID");
    const char* client_secret = std::getenv("QWEN_CLIENT_SECRET");
    
    if (!client_id || !client_secret) {
        std::cerr << "Error: QWEN_CLIENT_ID and QWEN_CLIENT_SECRET environment variables must be set\n";
        return "";
    }
    using namespace agent::llm;
    
    // Use a local redirect URI
    std::string redirect_uri = "http://localhost:8080/callback";
    
    // Initiate OAuth flow
    std::string auth_url = QwenOAuthHelper::initiate_oauth_flow(
        client_id,
        redirect_uri,
        "api_invoke"
    );
    
    // Create IO context for the server
    asio::io_context server_io_ctx;
    
    // Start the local server to handle callback
    std::cout << "Starting local server to handle OAuth callback...\n";
    std::cout << "Opening authentication URL in your default browser...\n";
    
    OAuthCallbackServer server(server_io_ctx);
    
    // Try to open the browser
    std::string browser_cmd;
#ifdef __APPLE__
    browser_cmd = "open \"" + auth_url + "\"";
#elif defined(_WIN32)
    browser_cmd = "start \"\" \"" + auth_url + "\"";
#else
    browser_cmd = "xdg-open \"" + auth_url + "\"";
#endif
    
    int result = std::system(browser_cmd.c_str());
    if (result != 0) {
        std::cout << "Could not open browser automatically. Please visit this URL manually:\n";
        std::cout << auth_url << std::endl;
    }
    
    // Run server in a separate thread
    std::promise<std::string> promise;
    std::future<std::string> future = promise.get_future();
    
    std::thread server_thread([&server, &promise, &server_io_ctx]() {
        try {
            std::string code = server.wait_for_code();
            promise.set_value(code);
            server_io_ctx.stop();
        } catch (const std::exception& e) {
            std::cerr << "Error in OAuth server: " << e.what() << std::endl;
            promise.set_value("");
        }
    });
    
    server_io_ctx.run();
    
    server_thread.join();
    
    std::string auth_code = future.get();
    
    if (auth_code.empty()) {
        std::cerr << "Failed to get authorization code. Timeout or error occurred.\n";
        return "";
    }
    
    std::cout << "Received authorization code. Exchanging for access token...\n";
    
    // Exchange authorization code for access token
    auto access_token = QwenOAuthHelper::exchange_code_for_token(
        client_id,
        client_secret,
        auth_code,
        redirect_uri
    );
    
    if (access_token) {
        std::cout << "Successfully obtained access token!\n";
        return *access_token;
    } else {
        std::cerr << "Failed to obtain access token!\n";
        return "";
    }
}

// Helper function to perform OAuth flow if needed (original version)
std::string perform_qwen_oauth_if_needed() {
    const char* client_id = std::getenv("QWEN_CLIENT_ID");
    const char* redirect_uri = std::getenv("QWEN_REDIRECT_URI");
    
    if (!client_id || !redirect_uri) {
        // Client ID or redirect URI not set, can't perform OAuth
        return "";
    }
    using namespace agent::llm;

    // Initiate OAuth flow
    std::string auth_url = QwenOAuthHelper::initiate_oauth_flow(
        client_id,
        redirect_uri,
        "api_invoke"
    );
    
    std::cout << "Qwen OAuth required. Please visit the following URL to authenticate:\n";
    std::cout << auth_url << std::endl;
    std::cout << "After authorizing, you will receive an authorization code.\n";
    std::cout << "Enter the authorization code here: ";
    
    std::string auth_code;
    std::getline(std::cin, auth_code);
    
    const char* client_secret = std::getenv("QWEN_CLIENT_SECRET");
    if (!client_secret) {
        std::cerr << "Error: QWEN_CLIENT_SECRET environment variable not set\n";
        return "";
    }
    
    // Exchange authorization code for access token
    auto access_token = QwenOAuthHelper::exchange_code_for_token(
        client_id,
        client_secret,
        auth_code,
        redirect_uri
    );
    
    if (access_token) {
        std::cout << "Successfully obtained access token!\n";
        return *access_token;
    } else {
        std::cerr << "Failed to obtain access token!\n";
        return "";
    }
}

int main(int argc, char* argv[]) {
  // ===== 加载配置 =====
  Config config = Config::load_default();

  const char* openai_key = std::getenv("OPENAI_API_KEY");
  const char* anthropic_key = std::getenv("ANTHROPIC_API_KEY");
  std::string qwen_oauth_token_value = std::getenv("QWEN_OAUTH_TOKEN") ? std::getenv("QWEN_OAUTH_TOKEN") : "";
  if (!anthropic_key) anthropic_key = std::getenv("ANTHROPIC_AUTH_TOKEN");

  if (anthropic_key) {
    const char* base_url = std::getenv("ANTHROPIC_BASE_URL");
    const char* model = std::getenv("ANTHROPIC_MODEL");
    config.providers["anthropic"] = ProviderConfig{"anthropic", anthropic_key, base_url ? base_url : "https://api.anthropic.com", std::nullopt, {}};
    if (model) config.default_model = model;
  }

  if (openai_key) {
    const char* base_url = std::getenv("OPENAI_BASE_URL");
    const char* model = std::getenv("OPENAI_MODEL");
    config.providers["openai"] = ProviderConfig{"openai", openai_key, base_url ? base_url : "https://api.openai.com", std::nullopt, {}};
    if (model) {
      config.default_model = model;
    } else if (!anthropic_key && qwen_oauth_token_value.empty()) {
      config.default_model = "gpt-4o";
    }
  }

  // Check for Qwen OAuth configuration
  // If no Anthropic or OpenAI keys are provided, try to use Qwen OAuth
  if (!anthropic_key && !openai_key) {
    // No other providers, try Qwen
    if (!qwen_oauth_token_value.empty()) {
      // Use existing token if available
      const char* base_url = std::getenv("QWEN_BASE_URL");
      const char* model = std::getenv("QWEN_MODEL");
      
      config.providers["qwen"] = ProviderConfig{
          "qwen", 
          qwen_oauth_token_value,  // Store OAuth token as api_key
          base_url ? base_url : "https://dashscope.aliyuncs.com",  // Default Qwen API endpoint
          std::nullopt, 
          {{"Authorization", "Bearer " + qwen_oauth_token_value}}  // Add Authorization header
      };
      if (model) {
        config.default_model = model;
      } else {
        config.default_model = "qwen-max";  // Default to a Qwen model
      }
    } else {
      // No existing token, initiate OAuth flow if credentials are available
      const char* qwen_client_id = std::getenv("QWEN_CLIENT_ID");
      const char* qwen_client_secret = std::getenv("QWEN_CLIENT_SECRET");
      
      if (qwen_client_id && qwen_client_secret) {
        // OAuth credentials are available, initiate OAuth flow with local server
        std::cout << "Initiating Qwen OAuth flow with local server...\n";
        qwen_oauth_token_value = perform_qwen_oauth_with_local_server();
        
        if (!qwen_oauth_token_value.empty()) {
          // Successfully obtained token, create provider config
          const char* base_url = std::getenv("QWEN_BASE_URL");
          const char* model = std::getenv("QWEN_MODEL");
          
          config.providers["qwen"] = ProviderConfig{
              "qwen", 
              qwen_oauth_token_value,  // Store OAuth token as api_key
              base_url ? base_url : "https://dashscope.aliyuncs.com",  // Default Qwen API endpoint
              std::nullopt, 
              {{"Authorization", "Bearer " + qwen_oauth_token_value}}  // Add Authorization header
          };
          if (model) {
            config.default_model = model;
          } else {
            config.default_model = "qwen-max";  // Default to a Qwen model
          }
        } else {
          std::cerr << "Failed to obtain Qwen OAuth token. Please try again.\n";
          return 1;
        }
      } else {
        // Check if legacy redirect URI is provided
        const char* qwen_redirect_uri = std::getenv("QWEN_REDIRECT_URI");
        if (qwen_redirect_uri) {
          // Legacy method with manual code entry
          std::cout << "Initiating Qwen OAuth flow (legacy method)...\n";
          qwen_oauth_token_value = perform_qwen_oauth_if_needed();
          
          if (!qwen_oauth_token_value.empty()) {
            // Successfully obtained token, create provider config
            const char* base_url = std::getenv("QWEN_BASE_URL");
            const char* model = std::getenv("QWEN_MODEL");
            
            config.providers["qwen"] = ProviderConfig{
                "qwen", 
                qwen_oauth_token_value,  // Store OAuth token as api_key
                base_url ? base_url : "https://dashscope.aliyuncs.com",  // Default Qwen API endpoint
                std::nullopt, 
                {{"Authorization", "Bearer " + qwen_oauth_token_value}}  // Add Authorization header
            };
            if (model) {
              config.default_model = model;
            } else {
              config.default_model = "qwen-max";  // Default to a Qwen model
            }
          } else {
            std::cerr << "Failed to obtain Qwen OAuth token. Please try again.\n";
            return 1;
          }
        } else {
          // No OAuth credentials provided, but no other providers either
          std::cerr << "No API providers configured. Please set QWEN_CLIENT_ID and QWEN_CLIENT_SECRET to use Qwen OAuth with automatic browser flow.\n";
          return 1;
        }
      }
    }
  }

  if (!anthropic_key && !openai_key && !config.providers.count("qwen")) {
    std::cerr << "Error: No API key found. Set one of the following:\n";
    std::cerr << "  - ANTHROPIC_API_KEY or ANTHROPIC_AUTH_TOKEN\n";
    std::cerr << "  - OPENAI_API_KEY\n";
    std::cerr << "  - QWEN_OAUTH_TOKEN (for existing OAuth token)\n";
    std::cerr << "  - Or set QWEN_CLIENT_ID and QWEN_CLIENT_SECRET to initiate OAuth flow with automatic browser authentication\n";
    return 1;
  }

  // ===== 初始化框架 =====
  asio::io_context io_ctx;
  agent::init();
  auto store = std::make_shared<JsonMessageStore>(config_paths::config_dir() / "sessions");
  auto session = Session::create(io_ctx, config, AgentType::Build, store);

  std::thread io_thread([&io_ctx]() {
    auto work = asio::make_work_guard(io_ctx);
    io_ctx.run();
  });

  // ===== FTXUI 屏幕 =====
  auto screen = ScreenInteractive::Fullscreen();
  screen.TrackMouse(true);

  // ===== 状态与上下文 =====
  AppState state;
  state.agent_state.set_model(config.default_model);
  state.agent_state.set_session_id(session->id());
  state.agent_state.update_context(session->estimated_context_tokens(), session->context_window());

  // 加载历史记录
  auto history_file = config_paths::config_dir() / "input_history.json";
  state.load_history_from_file(history_file);

  AppContext ctx{io_ctx, config, store, session, [&screen]() {
                   screen.Post(Event::Custom);
                 }};

  setup_tui_callbacks(state, ctx);

  // ===== 输入组件 =====
  auto input_option = InputOption();
  input_option.multiline = false;
  input_option.cursor_position = &state.input_cursor_pos;
  input_option.transform = [](InputState s) {
    if (s.is_placeholder) {
      s.element |= dim | color(Color::GrayDark);
    }
    return s.element;
  };
  input_option.on_change = [&state] {
    if (!state.input_text.empty() && state.input_text[0] == '/') {
      auto matches = match_commands(state.input_text);
      state.show_cmd_menu = !matches.empty();
      state.cmd_menu_selected = 0;
      state.show_file_path_menu = false;  // 确保文件路径菜单关闭
    } else {
      state.show_cmd_menu = false;

      // 检查是否输入了 @ 符号以启用文件路径自动完成
      size_t at_pos = state.input_text.rfind('@');
      if (at_pos != std::string::npos) {
        std::string path_prefix = state.input_text.substr(at_pos + 1);
        state.file_path_matches = match_file_paths(path_prefix);
        state.show_file_path_menu = !state.file_path_matches.empty();
        state.file_path_menu_selected = 0;
      } else {
        state.show_file_path_menu = false;
        state.file_path_matches.clear();
      }
    }
  };
  input_option.on_enter = [&] {
    handle_submit(state, ctx, screen);
  };
  auto input_component = Input(&state.input_text, "输入您的消息或 @ 文件路径", input_option);

  auto input_with_prompt = Renderer(input_component, [&] {
    return hbox({
        text(" > ") | bold | color(Color::Cyan),
        input_component->Render() | flex,
    });
  });

  // ===== 主渲染器 =====
  auto final_renderer = Renderer(input_with_prompt, [&] {
    auto status_bar = build_status_bar(state);
    auto chat_view = build_chat_view(state);
    auto cmd_menu_element = build_cmd_menu(state);
    auto file_path_menu_element = build_file_path_menu(state);

    auto mode_str = to_string(state.agent_state.mode());
    auto input_area = vbox({
        cmd_menu_element,
        file_path_menu_element,
        separator() | dim,
        input_with_prompt->Render(),
        separator() | dim,
        hbox({text(" " + mode_str + " ") | dim, text("  tab to switch mode") | dim, filler()}),
    });

    // Question 面板（优先级最高）
    if (state.show_question_panel) {
      auto question_panel = build_question_panel(state);
      return vbox({
          status_bar,
          separator() | dim,
          question_panel | flex,
      });
    }

    if (state.show_sessions_panel) {
      auto sessions_panel = build_sessions_panel(state);
      return vbox({
          status_bar,
          separator() | dim,
          sessions_panel | flex,
          input_area,
      });
    }

    return vbox({
        status_bar,
        separator() | dim,
        chat_view | flex,
        input_area,
    });
  });

  // ===== 事件处理 =====
  auto component = CatchEvent(final_renderer, [&](Event event) {
    return handle_main_event(state, ctx, screen, event);
  });

  // ===== 欢迎消息 =====
  state.chat_log.push(
      {EntryKind::SystemInfo, std::string("agent_cli ") + AGENT_SDK_VERSION_STRING + " — Type a message to start. /help for commands.", ""});

  // ===== 使用 Loop 手动控制循环 =====
  Loop loop(&screen, component);

  // 在 FTXUI 初始化终端后，禁用 ISIG 让 Ctrl+C 作为字符输入而非信号
  {
    struct termios term;
    tcgetattr(STDIN_FILENO, &term);
    term.c_lflag &= ~ISIG;  // 禁用信号生成（SIGINT, SIGQUIT, SIGTSTP）
    tcsetattr(STDIN_FILENO, TCSANOW, &term);
  }

  while (!loop.HasQuitted()) {
    loop.RunOnce();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // ===== 清理 =====
  // 保存历史记录
  state.save_history_to_file(history_file);

  ctx.session->cancel();
  io_ctx.stop();
  if (io_thread.joinable()) io_thread.join();

  return 0;
}
