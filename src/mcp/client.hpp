#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "core/config.hpp"
#include "core/types.hpp"
#include "tool/tool.hpp"
#include "transport.hpp"

namespace agent::mcp {

using json = nlohmann::json;

// MCP server capabilities (returned during initialize)
struct ServerCapabilities {
  bool supports_tools = false;
  bool supports_resources = false;
  bool supports_prompts = false;
  bool supports_logging = false;
};

// MCP tool definition (from tools/list)
struct McpToolInfo {
  std::string name;
  std::string description;
  json input_schema;  // JSON Schema
};

// MCP client state
enum class ClientState { Disconnected, Connecting, Initializing, Ready, Failed };

std::string to_string(ClientState state);

// MCP client — manages connection to a single MCP server
class McpClient {
 public:
  explicit McpClient(const McpServerConfig& config);
  ~McpClient();

  // Lifecycle
  std::future<bool> connect();
  void disconnect();

  // State
  ClientState state() const;
  bool is_ready() const {
    return state() == ClientState::Ready;
  }
  const std::string& server_name() const {
    return config_.name;
  }

  // Tool operations
  std::vector<McpToolInfo> list_tools();
  std::future<json> call_tool(const std::string& name, const json& arguments);

  // Server info
  const ServerCapabilities& capabilities() const {
    return capabilities_;
  }

 private:
  // Initialize handshake (called after transport connects)
  bool initialize();

  McpServerConfig config_;
  std::unique_ptr<Transport> transport_;
  ServerCapabilities capabilities_;
  ClientState state_ = ClientState::Disconnected;
  int64_t next_request_id_ = 1;
  mutable std::mutex mutex_;
};

// McpToolBridge — wraps an MCP tool as a local Tool for ToolRegistry
class McpToolBridge : public SimpleTool {
 public:
  McpToolBridge(std::shared_ptr<McpClient> client, const McpToolInfo& tool_info);

  std::vector<ParameterSchema> parameters() const override;
  std::future<ToolResult> execute(const json& args, const ToolContext& ctx) override;

 private:
  std::shared_ptr<McpClient> client_;
  McpToolInfo tool_info_;
};

// McpManager — singleton that manages all MCP server connections
class McpManager {
 public:
  static McpManager& instance();

  // Initialize all MCP servers from config
  void initialize(const std::vector<McpServerConfig>& servers);

  // Connect to all enabled servers
  void connect_all();

  // Disconnect all servers
  void disconnect_all();

  // Get a client by server name
  std::shared_ptr<McpClient> get_client(const std::string& name) const;

  // Get all connected clients
  std::vector<std::shared_ptr<McpClient>> all_clients() const;

  // Register MCP tools into ToolRegistry
  void register_tools();

  // Unregister all MCP tools from ToolRegistry
  void unregister_tools();

  // Get count of registered MCP tools
  size_t tool_count() const;

 private:
  McpManager() = default;

  mutable std::mutex mutex_;
  std::map<std::string, std::shared_ptr<McpClient>> clients_;
  std::vector<std::string> registered_tool_ids_;  // Track registered tool IDs for cleanup
};

}  // namespace agent::mcp
