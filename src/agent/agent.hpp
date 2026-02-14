#pragma once

// Core types
#include "core/config.hpp"
#include "core/message.hpp"
#include "core/types.hpp"
#include "core/uuid.hpp"

// Event bus
#include "bus/bus.hpp"

// Network
#include "net/http_client.hpp"
#include "net/sse_client.hpp"

// LLM providers
#include "llm/provider.hpp"

// Tool system
#include "tool/builtin/builtins.hpp"
#include "tool/tool.hpp"

// Skill system
#include "skill/skill.hpp"

// MCP client
#include "mcp/client.hpp"

// Session management
#include "session/session.hpp"

namespace agent {

// Initialize the agent framework
// Registers providers, builtin tools, and discovers skills from cwd
void init();

// Shutdown the agent framework
void shutdown();

// Get version string
std::string version();

}  // namespace agent
