#pragma once

#include <functional>
#include <future>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace agent::mcp {

using json = nlohmann::json;

// JSON-RPC 2.0 message types
struct JsonRpcRequest {
  std::string method;
  json params = json::object();
  int64_t id = 0;

  json to_json() const;
};

struct JsonRpcResponse {
  int64_t id = 0;
  std::optional<json> result;
  std::optional<json> error;

  bool ok() const {
    return !error.has_value();
  }

  std::string error_message() const;

  static JsonRpcResponse from_json(const json& j);
};

struct JsonRpcNotification {
  std::string method;
  json params = json::object();

  json to_json() const;
};

// Transport state
enum class TransportState { Disconnected, Connecting, Connected, Failed };

std::string to_string(TransportState state);

// Abstract transport interface for MCP communication
class Transport {
 public:
  virtual ~Transport() = default;

  // Send a JSON-RPC request and wait for response
  virtual std::future<JsonRpcResponse> send_request(const JsonRpcRequest& request) = 0;

  // Send a notification (no response expected)
  virtual void send_notification(const JsonRpcNotification& notification) = 0;

  // Set handler for incoming notifications from server
  using NotificationHandler = std::function<void(const std::string& method, const json& params)>;
  virtual void set_notification_handler(NotificationHandler handler) = 0;

  // Lifecycle
  virtual std::future<bool> connect() = 0;
  virtual void disconnect() = 0;

  // State
  virtual TransportState state() const = 0;
  virtual bool is_connected() const {
    return state() == TransportState::Connected;
  }
};

// Stdio transport — communicates with a local MCP server via stdin/stdout
class StdioTransport : public Transport {
 public:
  StdioTransport(std::string command, std::vector<std::string> args, std::map<std::string, std::string> env = {});
  ~StdioTransport() override;

  std::future<JsonRpcResponse> send_request(const JsonRpcRequest& request) override;
  void send_notification(const JsonRpcNotification& notification) override;
  void set_notification_handler(NotificationHandler handler) override;

  std::future<bool> connect() override;
  void disconnect() override;
  TransportState state() const override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// SSE transport — communicates with a remote MCP server via HTTP+SSE
class SseTransport : public Transport {
 public:
  SseTransport(std::string url, std::map<std::string, std::string> headers = {});
  ~SseTransport() override;

  std::future<JsonRpcResponse> send_request(const JsonRpcRequest& request) override;
  void send_notification(const JsonRpcNotification& notification) override;
  void set_notification_handler(NotificationHandler handler) override;

  std::future<bool> connect() override;
  void disconnect() override;
  TransportState state() const override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace agent::mcp
