#include "mcp/transport.hpp"

#include <spdlog/spdlog.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>
#include <unordered_map>

#include "net/http_client.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#endif

namespace agent::mcp {

// ============================================================
// JSON-RPC 2.0 serialization
// ============================================================

json JsonRpcRequest::to_json() const {
  json j;
  j["jsonrpc"] = "2.0";
  j["method"] = method;
  j["id"] = id;
  if (!params.empty()) {
    j["params"] = params;
  }
  return j;
}

std::string JsonRpcResponse::error_message() const {
  if (!error.has_value()) return "";
  auto &err = error.value();
  if (err.contains("message")) {
    return err["message"].get<std::string>();
  }
  return err.dump();
}

JsonRpcResponse JsonRpcResponse::from_json(const json &j) {
  JsonRpcResponse resp;
  if (j.contains("id") && !j["id"].is_null()) {
    resp.id = j["id"].get<int64_t>();
  }
  if (j.contains("result")) {
    resp.result = j["result"];
  }
  if (j.contains("error")) {
    resp.error = j["error"];
  }
  return resp;
}

json JsonRpcNotification::to_json() const {
  json j;
  j["jsonrpc"] = "2.0";
  j["method"] = method;
  if (!params.empty()) {
    j["params"] = params;
  }
  return j;
}

std::string to_string(TransportState state) {
  switch (state) {
    case TransportState::Disconnected:
      return "Disconnected";
    case TransportState::Connecting:
      return "Connecting";
    case TransportState::Connected:
      return "Connected";
    case TransportState::Failed:
      return "Failed";
  }
  return "Unknown";
}

// ============================================================
// StdioTransport::Impl — POSIX子进程通信
// ============================================================

#ifndef _WIN32

class StdioTransport::Impl {
 public:
  Impl(std::string command, std::vector<std::string> args, std::map<std::string, std::string> env)
      : command_(std::move(command)), args_(std::move(args)), env_(std::move(env)) {}

  ~Impl() {
    disconnect();
  }

  std::future<bool> connect() {
    return std::async(std::launch::async, [this]() -> bool {
      std::lock_guard<std::mutex> lock(process_mutex_);

      if (state_ == TransportState::Connected) return true;
      state_ = TransportState::Connecting;

      // Create pipes: parent writes to child stdin, reads from child stdout
      int stdin_pipe[2];   // [read-end, write-end]
      int stdout_pipe[2];  // [read-end, write-end]

      if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
        spdlog::error("[MCP] Failed to create pipes: {}", strerror(errno));
        state_ = TransportState::Failed;
        return false;
      }

      pid_ = fork();
      if (pid_ < 0) {
        spdlog::error("[MCP] Fork failed: {}", strerror(errno));
        state_ = TransportState::Failed;
        return false;
      }

      if (pid_ == 0) {
        // Child process
        close(stdin_pipe[1]);   // Close write end of stdin pipe
        close(stdout_pipe[0]);  // Close read end of stdout pipe

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);

        // Redirect stderr to /dev/null to avoid mixing with protocol
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
          dup2(devnull, STDERR_FILENO);
          close(devnull);
        }

        // Set environment variables
        for (const auto &[key, val] : env_) {
          setenv(key.c_str(), val.c_str(), 1);
        }

        // Build argv
        std::vector<const char *> argv;
        argv.push_back(command_.c_str());
        for (const auto &arg : args_) {
          argv.push_back(arg.c_str());
        }
        argv.push_back(nullptr);

        execvp(command_.c_str(), const_cast<char *const *>(argv.data()));

        // If exec fails
        _exit(127);
      }

      // Parent process
      close(stdin_pipe[0]);   // Close read end of stdin pipe
      close(stdout_pipe[1]);  // Close write end of stdout pipe

      write_fd_ = stdin_pipe[1];
      read_fd_ = stdout_pipe[0];

      stopped_ = false;
      state_ = TransportState::Connected;

      // Start reader thread
      reader_thread_ = std::thread([this]() {
        reader_loop();
      });

      spdlog::info("[MCP] Stdio transport connected (pid: {})", pid_);
      return true;
    });
  }

  void disconnect() {
    stopped_ = true;

    if (write_fd_ >= 0) {
      close(write_fd_);
      write_fd_ = -1;
    }
    if (read_fd_ >= 0) {
      close(read_fd_);
      read_fd_ = -1;
    }

    if (reader_thread_.joinable()) {
      reader_thread_.join();
    }

    // Kill and reap child process
    if (pid_ > 0) {
      kill(pid_, SIGTERM);
      int status;
      waitpid(pid_, &status, WNOHANG);

      // If still alive after 100ms, force kill
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (waitpid(pid_, &status, WNOHANG) == 0) {
        kill(pid_, SIGKILL);
        waitpid(pid_, &status, 0);
      }
      pid_ = -1;
    }

    // Fail all pending requests
    {
      std::lock_guard<std::mutex> lock(pending_mutex_);
      for (auto &[id, promise] : pending_requests_) {
        JsonRpcResponse err_resp;
        err_resp.id = id;
        err_resp.error = json{{"code", -32000}, {"message", "Transport disconnected"}};
        promise.set_value(std::move(err_resp));
      }
      pending_requests_.clear();
    }

    state_ = TransportState::Disconnected;
  }

  std::future<JsonRpcResponse> send_request(const JsonRpcRequest &request) {
    std::promise<JsonRpcResponse> promise;
    auto future = promise.get_future();

    if (state_ != TransportState::Connected) {
      JsonRpcResponse err_resp;
      err_resp.id = request.id;
      err_resp.error = json{{"code", -32000}, {"message", "Transport not connected"}};
      promise.set_value(std::move(err_resp));
      return future;
    }

    {
      std::lock_guard<std::mutex> lock(pending_mutex_);
      pending_requests_[request.id] = std::move(promise);
    }

    write_message(request.to_json());
    return future;
  }

  void send_notification(const JsonRpcNotification &notification) {
    if (state_ != TransportState::Connected) return;
    write_message(notification.to_json());
  }

  void set_notification_handler(Transport::NotificationHandler handler) {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    notification_handler_ = std::move(handler);
  }

  TransportState state() const {
    return state_;
  }

 private:
  void write_message(const json &msg) {
    std::string body = msg.dump();
    std::string header = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    std::string full = header + body;

    std::lock_guard<std::mutex> lock(write_mutex_);
    if (write_fd_ >= 0) {
      ssize_t written = write(write_fd_, full.data(), full.size());
      if (written < 0) {
        spdlog::error("[MCP] Write failed: {}", strerror(errno));
      }
    }
  }

  void reader_loop() {
    std::string buffer;
    std::array<char, 4096> read_buf;

    while (!stopped_) {
      ssize_t n = read(read_fd_, read_buf.data(), read_buf.size());
      if (n <= 0) {
        if (!stopped_) {
          spdlog::warn("[MCP] Reader: pipe closed or error");
          state_ = TransportState::Failed;
        }
        break;
      }

      buffer.append(read_buf.data(), n);

      // Parse messages from buffer (Content-Length header based framing)
      while (true) {
        auto header_end = buffer.find("\r\n\r\n");
        if (header_end == std::string::npos) break;

        // Parse Content-Length
        auto cl_pos = buffer.find("Content-Length: ");
        if (cl_pos == std::string::npos || cl_pos > header_end) {
          // Malformed header, skip to after \r\n\r\n
          buffer.erase(0, header_end + 4);
          continue;
        }

        auto cl_value_start = cl_pos + 16;  // strlen("Content-Length: ")
        auto cl_value_end = buffer.find("\r\n", cl_value_start);
        int content_length = std::stoi(buffer.substr(cl_value_start, cl_value_end - cl_value_start));

        size_t body_start = header_end + 4;
        if (buffer.size() < body_start + content_length) {
          break;  // Not enough data yet
        }

        std::string body = buffer.substr(body_start, content_length);
        buffer.erase(0, body_start + content_length);

        try {
          auto msg = json::parse(body);
          handle_incoming(msg);
        } catch (const std::exception &e) {
          spdlog::warn("[MCP] Failed to parse JSON message: {}", e.what());
        }
      }
    }
  }

  void handle_incoming(const json &msg) {
    // Check if it's a response (has "id" and either "result" or "error")
    if (msg.contains("id") && !msg["id"].is_null() && (msg.contains("result") || msg.contains("error"))) {
      auto resp = JsonRpcResponse::from_json(msg);
      std::lock_guard<std::mutex> lock(pending_mutex_);
      auto it = pending_requests_.find(resp.id);
      if (it != pending_requests_.end()) {
        it->second.set_value(std::move(resp));
        pending_requests_.erase(it);
      }
      return;
    }

    // It's a notification (no "id" or null "id", has "method")
    if (msg.contains("method")) {
      std::string method = msg["method"].get<std::string>();
      json params = msg.value("params", json::object());

      std::lock_guard<std::mutex> lock(handler_mutex_);
      if (notification_handler_) {
        notification_handler_(method, params);
      }
    }
  }

  std::string command_;
  std::vector<std::string> args_;
  std::map<std::string, std::string> env_;

  pid_t pid_ = -1;
  int write_fd_ = -1;
  int read_fd_ = -1;

  std::atomic<TransportState> state_{TransportState::Disconnected};
  std::atomic<bool> stopped_{false};

  std::thread reader_thread_;

  std::mutex write_mutex_;
  std::mutex process_mutex_;

  std::mutex pending_mutex_;
  std::unordered_map<int64_t, std::promise<JsonRpcResponse>> pending_requests_;

  std::mutex handler_mutex_;
  Transport::NotificationHandler notification_handler_;
};

#else  // _WIN32

class StdioTransport::Impl {
 public:
  Impl(std::string command, std::vector<std::string> args, std::map<std::string, std::string> env)
      : command_(std::move(command)), args_(std::move(args)), env_(std::move(env)) {}

  ~Impl() {
    disconnect();
  }

  std::future<bool> connect() {
    return std::async(std::launch::async, []() -> bool {
      spdlog::error("[MCP] Stdio transport not implemented on Windows");
      return false;
    });
  }

  void disconnect() {}
  std::future<JsonRpcResponse> send_request(const JsonRpcRequest &request) {
    std::promise<JsonRpcResponse> p;
    JsonRpcResponse resp;
    resp.id = request.id;
    resp.error = json{{"code", -32000}, {"message", "Not implemented on Windows"}};
    p.set_value(std::move(resp));
    return p.get_future();
  }
  void send_notification(const JsonRpcNotification &) {}
  void set_notification_handler(Transport::NotificationHandler) {}
  TransportState state() const {
    return TransportState::Failed;
  }

 private:
  std::string command_;
  std::vector<std::string> args_;
  std::map<std::string, std::string> env_;
};

#endif

// ============================================================
// StdioTransport — delegates to Impl
// ============================================================

StdioTransport::StdioTransport(std::string command, std::vector<std::string> args, std::map<std::string, std::string> env)
    : impl_(std::make_unique<Impl>(std::move(command), std::move(args), std::move(env))) {}

StdioTransport::~StdioTransport() = default;

std::future<JsonRpcResponse> StdioTransport::send_request(const JsonRpcRequest &request) {
  return impl_->send_request(request);
}

void StdioTransport::send_notification(const JsonRpcNotification &notification) {
  impl_->send_notification(notification);
}

void StdioTransport::set_notification_handler(NotificationHandler handler) {
  impl_->set_notification_handler(std::move(handler));
}

std::future<bool> StdioTransport::connect() {
  return impl_->connect();
}

void StdioTransport::disconnect() {
  impl_->disconnect();
}

TransportState StdioTransport::state() const {
  return impl_->state();
}

// ============================================================
// SseTransport::Impl — HTTP+SSE通信
// ============================================================

class SseTransport::Impl {
 public:
  Impl(std::string url, std::map<std::string, std::string> headers) : url_(std::move(url)), headers_(std::move(headers)) {}

  ~Impl() {
    disconnect();
  }

  std::future<bool> connect() {
    return std::async(std::launch::async, [this]() -> bool {
      state_ = TransportState::Connecting;

      // For SSE transport, the connection is established when we first send a request.
      // The MCP SSE protocol uses:
      //   - POST to send JSON-RPC messages
      //   - SSE stream to receive responses
      // For now, mark as connected — actual SSE stream will be set up on first request.
      state_ = TransportState::Connected;
      spdlog::info("[MCP] SSE transport ready for: {}", url_);
      return true;
    });
  }

  void disconnect() {
    stopped_ = true;
    state_ = TransportState::Disconnected;

    // Fail all pending requests
    std::lock_guard<std::mutex> lock(pending_mutex_);
    for (auto &[id, promise] : pending_requests_) {
      JsonRpcResponse err_resp;
      err_resp.id = id;
      err_resp.error = json{{"code", -32000}, {"message", "Transport disconnected"}};
      promise.set_value(std::move(err_resp));
    }
    pending_requests_.clear();
  }

  std::future<JsonRpcResponse> send_request(const JsonRpcRequest &request) {
    std::promise<JsonRpcResponse> promise;
    auto future = promise.get_future();

    if (state_ != TransportState::Connected) {
      JsonRpcResponse err_resp;
      err_resp.id = request.id;
      err_resp.error = json{{"code", -32000}, {"message", "Transport not connected"}};
      promise.set_value(std::move(err_resp));
      return future;
    }

    // Store pending request
    {
      std::lock_guard<std::mutex> lock(pending_mutex_);
      pending_requests_[request.id] = std::move(promise);
    }

    // Send HTTP POST with JSON-RPC body
    std::thread([this, request]() {
      try {
        // Create a temporary io_context for this request
        asio::io_context io_ctx;
        auto http = std::make_unique<agent::net::HttpClient>(io_ctx);

        agent::net::HttpOptions opts;
        opts.method = "POST";
        opts.headers = headers_;
        opts.headers["Content-Type"] = "application/json";
        opts.body = request.to_json().dump();

        auto response_future = http->request(url_, opts);
        io_ctx.run();

        auto response = response_future.get();

        if (!response.ok()) {
          std::lock_guard<std::mutex> lock(pending_mutex_);
          auto it = pending_requests_.find(request.id);
          if (it != pending_requests_.end()) {
            JsonRpcResponse err_resp;
            err_resp.id = request.id;
            err_resp.error = json{{"code", -32000}, {"message", "HTTP error: " + std::to_string(response.status_code) + " " + response.error}};
            it->second.set_value(std::move(err_resp));
            pending_requests_.erase(it);
          }
          return;
        }

        // Parse response body as JSON-RPC
        auto msg = json::parse(response.body);
        auto resp = JsonRpcResponse::from_json(msg);

        std::lock_guard<std::mutex> lock(pending_mutex_);
        auto it = pending_requests_.find(resp.id);
        if (it != pending_requests_.end()) {
          it->second.set_value(std::move(resp));
          pending_requests_.erase(it);
        }
      } catch (const std::exception &e) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        auto it = pending_requests_.find(request.id);
        if (it != pending_requests_.end()) {
          JsonRpcResponse err_resp;
          err_resp.id = request.id;
          err_resp.error = json{{"code", -32000}, {"message", std::string("Request failed: ") + e.what()}};
          it->second.set_value(std::move(err_resp));
          pending_requests_.erase(it);
        }
      }
    }).detach();

    return future;
  }

  void send_notification(const JsonRpcNotification &notification) {
    if (state_ != TransportState::Connected) return;

    // Fire and forget HTTP POST
    std::thread([this, notification]() {
      try {
        asio::io_context io_ctx;
        auto http = std::make_unique<agent::net::HttpClient>(io_ctx);

        agent::net::HttpOptions opts;
        opts.method = "POST";
        opts.headers = headers_;
        opts.headers["Content-Type"] = "application/json";
        opts.body = notification.to_json().dump();

        http->request(url_, opts);
        io_ctx.run();
      } catch (const std::exception &e) {
        spdlog::warn("[MCP] SSE notification send failed: {}", e.what());
      }
    }).detach();
  }

  void set_notification_handler(Transport::NotificationHandler handler) {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    notification_handler_ = std::move(handler);
  }

  TransportState state() const {
    return state_;
  }

 private:
  std::string url_;
  std::map<std::string, std::string> headers_;

  std::atomic<TransportState> state_{TransportState::Disconnected};
  std::atomic<bool> stopped_{false};

  std::mutex pending_mutex_;
  std::unordered_map<int64_t, std::promise<JsonRpcResponse>> pending_requests_;

  std::mutex handler_mutex_;
  Transport::NotificationHandler notification_handler_;
};

// ============================================================
// SseTransport — delegates to Impl
// ============================================================

SseTransport::SseTransport(std::string url, std::map<std::string, std::string> headers)
    : impl_(std::make_unique<Impl>(std::move(url), std::move(headers))) {}

SseTransport::~SseTransport() = default;

std::future<JsonRpcResponse> SseTransport::send_request(const JsonRpcRequest &request) {
  return impl_->send_request(request);
}

void SseTransport::send_notification(const JsonRpcNotification &notification) {
  impl_->send_notification(notification);
}

void SseTransport::set_notification_handler(NotificationHandler handler) {
  impl_->set_notification_handler(std::move(handler));
}

std::future<bool> SseTransport::connect() {
  return impl_->connect();
}

void SseTransport::disconnect() {
  impl_->disconnect();
}

TransportState SseTransport::state() const {
  return impl_->state();
}

}  // namespace agent::mcp
