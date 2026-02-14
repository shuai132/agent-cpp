#include "mcp/client.hpp"

#include <spdlog/spdlog.h>

#include "bus/bus.hpp"

namespace agent::mcp {

// ============================================================
// ClientState helpers
// ============================================================

std::string to_string(ClientState state) {
  switch (state) {
    case ClientState::Disconnected:
      return "Disconnected";
    case ClientState::Connecting:
      return "Connecting";
    case ClientState::Initializing:
      return "Initializing";
    case ClientState::Ready:
      return "Ready";
    case ClientState::Failed:
      return "Failed";
  }
  return "Unknown";
}

// ============================================================
// McpClient
// ============================================================

McpClient::McpClient(const McpServerConfig &config) : config_(config) {
  // Create transport based on server type
  if (config.type == "local" || config.type == "stdio") {
    transport_ = std::make_unique<StdioTransport>(config.command, config.args, config.env);
  } else if (config.type == "remote" || config.type == "sse") {
    transport_ = std::make_unique<SseTransport>(config.url, config.headers);
  } else {
    spdlog::error("[MCP] Unknown server type '{}' for server '{}'", config.type, config.name);
    state_ = ClientState::Failed;
  }
}

McpClient::~McpClient() {
  disconnect();
}

std::future<bool> McpClient::connect() {
  if (!transport_) {
    return std::async(std::launch::deferred, []() {
      return false;
    });
  }

  state_ = ClientState::Connecting;

  return std::async(std::launch::async, [this]() -> bool {
    // Connect transport
    auto transport_future = transport_->connect();
    if (!transport_future.get()) {
      spdlog::error("[MCP] Failed to connect transport for server '{}'", config_.name);
      state_ = ClientState::Failed;
      return false;
    }

    // Set notification handler
    transport_->set_notification_handler([this](const std::string &method, const json &params) {
      spdlog::debug("[MCP] Notification from '{}': {} {}", config_.name, method, params.dump());

      // Handle tools/list_changed notification
      if (method == "notifications/tools/list_changed") {
        Bus::instance().publish(events::McpToolsChanged{config_.name});
      }
    });

    // Perform MCP initialize handshake
    if (!initialize()) {
      spdlog::error("[MCP] Initialize handshake failed for server '{}'", config_.name);
      state_ = ClientState::Failed;
      return false;
    }

    state_ = ClientState::Ready;
    spdlog::info("[MCP] Server '{}' is ready", config_.name);
    return true;
  });
}

void McpClient::disconnect() {
  if (transport_) {
    transport_->disconnect();
  }
  state_ = ClientState::Disconnected;
}

ClientState McpClient::state() const {
  return state_;
}

bool McpClient::initialize() {
  state_ = ClientState::Initializing;

  JsonRpcRequest req;
  req.method = "initialize";
  req.id = next_request_id_++;
  req.params =
      json{{"protocolVersion", "2024-11-05"}, {"capabilities", json::object()}, {"clientInfo", json{{"name", "agent-cpp"}, {"version", "0.1.0"}}}};

  auto future = transport_->send_request(req);

  try {
    auto resp = future.get();
    if (!resp.ok()) {
      spdlog::error("[MCP] Initialize error from '{}': {}", config_.name, resp.error_message());
      return false;
    }

    // Parse server capabilities
    if (resp.result.has_value()) {
      auto &result = resp.result.value();
      if (result.contains("capabilities")) {
        auto &caps = result["capabilities"];
        capabilities_.supports_tools = caps.contains("tools");
        capabilities_.supports_resources = caps.contains("resources");
        capabilities_.supports_prompts = caps.contains("prompts");
        capabilities_.supports_logging = caps.contains("logging");
      }

      if (result.contains("serverInfo")) {
        auto &info = result["serverInfo"];
        spdlog::info("[MCP] Server '{}' info: {} v{}", config_.name, info.value("name", "unknown"), info.value("version", "unknown"));
      }
    }

    // Send initialized notification
    JsonRpcNotification notif;
    notif.method = "notifications/initialized";
    transport_->send_notification(notif);

    return true;
  } catch (const std::exception &e) {
    spdlog::error("[MCP] Initialize exception for '{}': {}", config_.name, e.what());
    return false;
  }
}

std::vector<McpToolInfo> McpClient::list_tools() {
  std::vector<McpToolInfo> tools;

  if (state_ != ClientState::Ready || !capabilities_.supports_tools) {
    return tools;
  }

  JsonRpcRequest req;
  req.method = "tools/list";
  req.id = next_request_id_++;

  auto future = transport_->send_request(req);

  try {
    auto resp = future.get();
    if (!resp.ok()) {
      spdlog::error("[MCP] tools/list error from '{}': {}", config_.name, resp.error_message());
      return tools;
    }

    if (resp.result.has_value() && resp.result->contains("tools")) {
      for (const auto &tool_json : (*resp.result)["tools"]) {
        McpToolInfo info;
        info.name = tool_json.value("name", "");
        info.description = tool_json.value("description", "");
        if (tool_json.contains("inputSchema")) {
          info.input_schema = tool_json["inputSchema"];
        } else {
          info.input_schema = json{{"type", "object"}, {"properties", json::object()}};
        }
        tools.push_back(std::move(info));
      }
    }

    spdlog::info("[MCP] Server '{}' provides {} tools", config_.name, tools.size());
  } catch (const std::exception &e) {
    spdlog::error("[MCP] tools/list exception for '{}': {}", config_.name, e.what());
  }

  return tools;
}

std::future<json> McpClient::call_tool(const std::string &name, const json &arguments) {
  return std::async(std::launch::async, [this, name, arguments]() -> json {
    if (state_ != ClientState::Ready) {
      return json{{"error", "MCP server not ready"}};
    }

    JsonRpcRequest req;
    req.method = "tools/call";
    req.id = next_request_id_++;
    req.params = json{{"name", name}, {"arguments", arguments}};

    auto future = transport_->send_request(req);

    try {
      auto resp = future.get();
      if (!resp.ok()) {
        return json{{"isError", true}, {"content", json::array({json{{"type", "text"}, {"text", resp.error_message()}}})}};
      }

      if (resp.result.has_value()) {
        return resp.result.value();
      }

      return json{{"content", json::array()}};
    } catch (const std::exception &e) {
      return json{{"isError", true}, {"content", json::array({json{{"type", "text"}, {"text", std::string("Exception: ") + e.what()}}})}};
    }
  });
}

// ============================================================
// McpToolBridge — wraps MCP tool as local Tool
// ============================================================

McpToolBridge::McpToolBridge(std::shared_ptr<McpClient> client, const McpToolInfo &tool_info)
    : SimpleTool("mcp_" + client->server_name() + "_" + tool_info.name, "[MCP:" + client->server_name() + "] " + tool_info.description),
      client_(std::move(client)),
      tool_info_(tool_info) {}

std::vector<ParameterSchema> McpToolBridge::parameters() const {
  std::vector<ParameterSchema> params;

  // Convert JSON Schema properties to ParameterSchema
  if (tool_info_.input_schema.contains("properties")) {
    auto &props = tool_info_.input_schema["properties"];
    auto required_fields = tool_info_.input_schema.value("required", json::array());

    for (auto it = props.begin(); it != props.end(); ++it) {
      ParameterSchema param;
      param.name = it.key();
      param.type = it.value().value("type", "string");
      param.description = it.value().value("description", "");

      // Check if required
      bool is_required = false;
      for (const auto &req : required_fields) {
        if (req.get<std::string>() == param.name) {
          is_required = true;
          break;
        }
      }
      param.required = is_required;

      if (it.value().contains("default")) {
        param.default_value = it.value()["default"];
      }

      if (it.value().contains("enum")) {
        std::vector<std::string> enum_vals;
        for (const auto &v : it.value()["enum"]) {
          enum_vals.push_back(v.get<std::string>());
        }
        param.enum_values = std::move(enum_vals);
      }

      params.push_back(std::move(param));
    }
  }

  return params;
}

std::future<ToolResult> McpToolBridge::execute(const json &args, const ToolContext &ctx) {
  return std::async(std::launch::async, [this, args]() -> ToolResult {
    if (!client_ || !client_->is_ready()) {
      return ToolResult::error("MCP server '" + client_->server_name() + "' is not ready");
    }

    try {
      auto result_future = client_->call_tool(tool_info_.name, args);
      auto result = result_future.get();

      // Extract text content from MCP tool result
      bool is_error = result.value("isError", false);
      std::string output;

      if (result.contains("content")) {
        for (const auto &content : result["content"]) {
          std::string type = content.value("type", "text");
          if (type == "text") {
            if (!output.empty()) output += "\n";
            output += content.value("text", "");
          }
        }
      }

      if (output.empty()) {
        output = result.dump(2);
      }

      if (is_error) {
        return ToolResult::error(output);
      }
      return ToolResult::success(output);
    } catch (const std::exception &e) {
      return ToolResult::error(std::string("MCP tool execution failed: ") + e.what());
    }
  });
}

// ============================================================
// McpManager — singleton
// ============================================================

McpManager &McpManager::instance() {
  static McpManager instance;
  return instance;
}

void McpManager::initialize(const std::vector<McpServerConfig> &servers) {
  std::lock_guard<std::mutex> lock(mutex_);

  for (const auto &config : servers) {
    if (!config.enabled) {
      spdlog::info("[MCP] Skipping disabled server '{}'", config.name);
      continue;
    }

    auto client = std::make_shared<McpClient>(config);
    clients_[config.name] = client;
    spdlog::info("[MCP] Registered server '{}' (type: {})", config.name, config.type);
  }
}

void McpManager::connect_all() {
  std::vector<std::future<bool>> futures;
  std::vector<std::string> names;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &[name, client] : clients_) {
      futures.push_back(client->connect());
      names.push_back(name);
    }
  }

  // Wait for all connections
  for (size_t i = 0; i < futures.size(); ++i) {
    try {
      bool success = futures[i].get();
      if (success) {
        spdlog::info("[MCP] Connected to server '{}'", names[i]);
      } else {
        spdlog::warn("[MCP] Failed to connect to server '{}'", names[i]);
      }
    } catch (const std::exception &e) {
      spdlog::error("[MCP] Exception connecting to '{}': {}", names[i], e.what());
    }
  }
}

void McpManager::disconnect_all() {
  unregister_tools();

  std::lock_guard<std::mutex> lock(mutex_);
  for (auto &[name, client] : clients_) {
    client->disconnect();
  }
  clients_.clear();
}

std::shared_ptr<McpClient> McpManager::get_client(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = clients_.find(name);
  if (it != clients_.end()) {
    return it->second;
  }
  return nullptr;
}

std::vector<std::shared_ptr<McpClient>> McpManager::all_clients() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::shared_ptr<McpClient>> result;
  for (const auto &[name, client] : clients_) {
    result.push_back(client);
  }
  return result;
}

void McpManager::register_tools() {
  // First unregister any previously registered tools
  unregister_tools();

  std::lock_guard<std::mutex> lock(mutex_);
  auto &registry = ToolRegistry::instance();

  for (auto &[name, client] : clients_) {
    if (!client->is_ready()) continue;

    auto tools = client->list_tools();
    for (const auto &tool_info : tools) {
      auto bridge = std::make_shared<McpToolBridge>(client, tool_info);
      std::string tool_id = bridge->id();
      registry.register_tool(bridge);
      registered_tool_ids_.push_back(tool_id);
      spdlog::info("[MCP] Registered tool '{}' from server '{}'", tool_id, name);
    }
  }
}

void McpManager::unregister_tools() {
  auto &registry = ToolRegistry::instance();
  for (const auto &id : registered_tool_ids_) {
    registry.unregister_tool(id);
  }
  registered_tool_ids_.clear();
}

size_t McpManager::tool_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return registered_tool_ids_.size();
}

}  // namespace agent::mcp
