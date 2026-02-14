#include <gtest/gtest.h>

#include "core/config.hpp"
#include "tool/permission.hpp"

using namespace agent;

class PermissionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    PermissionManager::instance().clear_cache();
  }

  void TearDown() override {
    PermissionManager::instance().clear_cache();
  }

  AgentConfig make_config(Permission default_perm = Permission::Allow) {
    AgentConfig config;
    config.id = "test-agent";
    config.type = AgentType::Build;
    config.default_permission = default_perm;
    return config;
  }
};

TEST_F(PermissionTest, DefaultAllow) {
  auto config = make_config(Permission::Allow);

  auto result = PermissionManager::instance().check_permission("bash", config);
  EXPECT_EQ(result, Permission::Allow);
}

TEST_F(PermissionTest, DeniedTool) {
  auto config = make_config(Permission::Allow);
  config.denied_tools = {"bash", "write"};

  EXPECT_EQ(PermissionManager::instance().check_permission("bash", config), Permission::Deny);
  EXPECT_EQ(PermissionManager::instance().check_permission("write", config), Permission::Deny);
  // Non-denied tool should still be allowed
  EXPECT_EQ(PermissionManager::instance().check_permission("read", config), Permission::Allow);
}

TEST_F(PermissionTest, AllowedToolsWhitelist) {
  auto config = make_config(Permission::Allow);
  config.allowed_tools = {"read", "glob"};

  // Whitelisted tools should be allowed
  EXPECT_EQ(PermissionManager::instance().check_permission("read", config), Permission::Allow);
  EXPECT_EQ(PermissionManager::instance().check_permission("glob", config), Permission::Allow);

  // Non-whitelisted tools should be denied
  EXPECT_EQ(PermissionManager::instance().check_permission("bash", config), Permission::Deny);
  EXPECT_EQ(PermissionManager::instance().check_permission("write", config), Permission::Deny);
}

TEST_F(PermissionTest, ExplicitPermission) {
  auto config = make_config(Permission::Ask);
  config.permissions["bash"] = Permission::Allow;
  config.permissions["write"] = Permission::Deny;

  EXPECT_EQ(PermissionManager::instance().check_permission("bash", config), Permission::Allow);
  EXPECT_EQ(PermissionManager::instance().check_permission("write", config), Permission::Deny);
  // Unlisted tool should fall through to default
  EXPECT_EQ(PermissionManager::instance().check_permission("read", config), Permission::Ask);
}

TEST_F(PermissionTest, CacheGrant) {
  auto config = make_config(Permission::Ask);

  // Before granting, should be Ask (default)
  EXPECT_EQ(PermissionManager::instance().check_permission("bash", config), Permission::Ask);

  // Grant permission
  PermissionManager::instance().grant("bash");

  // After granting, should be Allow
  EXPECT_EQ(PermissionManager::instance().check_permission("bash", config), Permission::Allow);

  // Verify via get_cached
  auto cached = PermissionManager::instance().get_cached("bash");
  ASSERT_TRUE(cached.has_value());
  EXPECT_EQ(*cached, Permission::Allow);
}

TEST_F(PermissionTest, CacheDeny) {
  auto config = make_config(Permission::Allow);

  // Before denying, should be Allow (default)
  EXPECT_EQ(PermissionManager::instance().check_permission("bash", config), Permission::Allow);

  // Deny permission
  PermissionManager::instance().deny("bash");

  // After denying, should be Deny
  EXPECT_EQ(PermissionManager::instance().check_permission("bash", config), Permission::Deny);

  // Verify via get_cached
  auto cached = PermissionManager::instance().get_cached("bash");
  ASSERT_TRUE(cached.has_value());
  EXPECT_EQ(*cached, Permission::Deny);
}

TEST_F(PermissionTest, ClearCache) {
  auto config = make_config(Permission::Ask);

  // Grant some permissions
  PermissionManager::instance().grant("bash");
  PermissionManager::instance().grant("write");

  // Verify they're cached
  ASSERT_TRUE(PermissionManager::instance().get_cached("bash").has_value());
  ASSERT_TRUE(PermissionManager::instance().get_cached("write").has_value());

  // Clear cache
  PermissionManager::instance().clear_cache();

  // Cache should be empty
  EXPECT_FALSE(PermissionManager::instance().get_cached("bash").has_value());
  EXPECT_FALSE(PermissionManager::instance().get_cached("write").has_value());

  // Should fall back to default
  EXPECT_EQ(PermissionManager::instance().check_permission("bash", config), Permission::Ask);
}

TEST_F(PermissionTest, Singleton) {
  auto &a = PermissionManager::instance();
  auto &b = PermissionManager::instance();

  EXPECT_EQ(&a, &b);
}
