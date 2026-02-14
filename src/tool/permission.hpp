#pragma once

#include <map>
#include <mutex>
#include <string>

#include "core/config.hpp"
#include "core/types.hpp"

namespace agent {

// Permission manager — checks and caches tool execution permissions
class PermissionManager {
 public:
  static PermissionManager &instance();

  // Check if a tool is allowed for a given agent config
  // Returns: Allow, Deny, or Ask
  Permission check_permission(const std::string &tool_id, const AgentConfig &agent_config) const;

  // Record a permanent permission decision (from user interaction)
  void grant(const std::string &tool_id);
  void deny(const std::string &tool_id);

  // Check if there's a cached decision for a tool
  std::optional<Permission> get_cached(const std::string &tool_id) const;

  // Clear all cached permissions
  void clear_cache();

 private:
  PermissionManager() = default;

  mutable std::mutex mutex_;
  std::map<std::string, Permission> cache_;  // Runtime permission cache
};

}  // namespace agent
