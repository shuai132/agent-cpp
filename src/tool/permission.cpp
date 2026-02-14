#include "tool/permission.hpp"

#include <algorithm>

namespace agent {

PermissionManager &PermissionManager::instance() {
  static PermissionManager instance;
  return instance;
}

Permission PermissionManager::check_permission(const std::string &tool_id, const AgentConfig &agent_config) const {
  std::lock_guard lock(mutex_);

  // 1. Check denied_tools — if tool is blacklisted, deny
  auto &denied = agent_config.denied_tools;
  if (std::find(denied.begin(), denied.end(), tool_id) != denied.end()) {
    return Permission::Deny;
  }

  // 2. Check allowed_tools — if whitelist is non-empty and tool is not in it, deny
  auto &allowed = agent_config.allowed_tools;
  if (!allowed.empty()) {
    if (std::find(allowed.begin(), allowed.end(), tool_id) == allowed.end()) {
      return Permission::Deny;
    }
  }

  // 3. Check explicit permissions map
  auto perm_it = agent_config.permissions.find(tool_id);
  if (perm_it != agent_config.permissions.end()) {
    return perm_it->second;
  }

  // 4. Check runtime cache
  auto cache_it = cache_.find(tool_id);
  if (cache_it != cache_.end()) {
    return cache_it->second;
  }

  // 5. Return default permission
  return agent_config.default_permission;
}

void PermissionManager::grant(const std::string &tool_id) {
  std::lock_guard lock(mutex_);
  cache_[tool_id] = Permission::Allow;
}

void PermissionManager::deny(const std::string &tool_id) {
  std::lock_guard lock(mutex_);
  cache_[tool_id] = Permission::Deny;
}

std::optional<Permission> PermissionManager::get_cached(const std::string &tool_id) const {
  std::lock_guard lock(mutex_);
  auto it = cache_.find(tool_id);
  if (it != cache_.end()) {
    return it->second;
  }
  return std::nullopt;
}

void PermissionManager::clear_cache() {
  std::lock_guard lock(mutex_);
  cache_.clear();
}

}  // namespace agent
