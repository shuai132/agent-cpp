#include <gtest/gtest.h>

#include <filesystem>

#include "core/config.hpp"

using namespace agent;

namespace fs = std::filesystem;

// --- ConfigTest ---

TEST(ConfigTest, LoadDefault) {
  auto config = Config::load_default();

  // 默认配置应有一个有效的 default_model
  EXPECT_FALSE(config.default_model.empty());
  // 默认 log_level 为 "info"
  EXPECT_EQ(config.log_level, "info");
}

TEST(ConfigTest, GetNonexistentProvider) {
  Config config;

  auto provider = config.get_provider("nonexistent");
  EXPECT_FALSE(provider.has_value());
}

TEST(ConfigTest, GetOrCreateAgent) {
  Config config;
  config.default_model = "test-model";

  // Build 代理
  auto build_agent = config.get_or_create_agent(AgentType::Build);
  EXPECT_EQ(build_agent.id, "build");
  EXPECT_EQ(build_agent.type, AgentType::Build);
  EXPECT_EQ(build_agent.model, "test-model");
  EXPECT_EQ(build_agent.default_permission, Permission::Ask);

  // Explore 代理：只读，拒绝写入工具
  auto explore_agent = config.get_or_create_agent(AgentType::Explore);
  EXPECT_EQ(explore_agent.id, "explore");
  EXPECT_EQ(explore_agent.default_permission, Permission::Allow);
  EXPECT_FALSE(explore_agent.denied_tools.empty());

  // Plan 代理：仅允许读取工具
  auto plan_agent = config.get_or_create_agent(AgentType::Plan);
  EXPECT_EQ(plan_agent.id, "plan");
  EXPECT_EQ(plan_agent.default_permission, Permission::Deny);
  EXPECT_FALSE(plan_agent.allowed_tools.empty());

  // Compaction 代理：无工具
  auto compaction_agent = config.get_or_create_agent(AgentType::Compaction);
  EXPECT_EQ(compaction_agent.id, "compaction");
  EXPECT_EQ(compaction_agent.default_permission, Permission::Deny);
  EXPECT_TRUE(compaction_agent.allowed_tools.empty());
}

TEST(ConfigTest, DefaultModel) {
  Config config;

  EXPECT_EQ(config.default_model, "claude-sonnet-4-20250514");
}

TEST(ConfigTest, ContextSettings) {
  Config config;

  EXPECT_EQ(config.context.prune_protect_tokens, 40000);
  EXPECT_EQ(config.context.prune_minimum_tokens, 20000);
  EXPECT_EQ(config.context.truncate_max_lines, 2000u);
  EXPECT_EQ(config.context.truncate_max_bytes, 51200u);
}

// --- ConfigPathsTest ---

TEST(ConfigPathsTest, HomeDir) {
  auto home = config_paths::home_dir();

  EXPECT_FALSE(home.empty());
  EXPECT_TRUE(fs::exists(home));
}

TEST(ConfigPathsTest, ConfigDir) {
  auto config_dir = config_paths::config_dir();

  EXPECT_FALSE(config_dir.empty());
  // 配置目录应以 "agent-sdk" 结尾
  EXPECT_EQ(config_dir.filename(), "agent-sdk");
  // 父目录应为 ".config"
  EXPECT_EQ(config_dir.parent_path().filename(), ".config");
}

TEST(ConfigPathsTest, FindGitRoot) {
  // 当前项目是一个 git 仓库，从当前目录能找到 git 根目录
  auto git_root = config_paths::find_git_root(fs::current_path());

  ASSERT_TRUE(git_root.has_value());
  // git 根目录应该包含 .git
  EXPECT_TRUE(fs::exists(*git_root / ".git"));

  // 从根目录搜索不应该找到 git 仓库（除非根目录恰好是 git 仓库）
  auto root_result = config_paths::find_git_root("/");
  // 不做严格断言，因为取决于环境，但不应崩溃
}

TEST(ConfigTest, SaveAndLoadMcpServers) {
  Config config;

  // 添加本地 MCP 服务器
  McpServerConfig local_server;
  local_server.name = "my-server";
  local_server.type = "local";
  local_server.command = "npx";
  local_server.args = {"-y", "@modelcontextprotocol/server-filesystem"};
  local_server.env = {{"HOME", "/tmp"}};
  local_server.enabled = true;
  config.mcp_servers.push_back(local_server);

  // 添加远程 MCP 服务器
  McpServerConfig remote_server;
  remote_server.name = "remote-server";
  remote_server.type = "remote";
  remote_server.url = "https://example.com/mcp";
  remote_server.headers = {{"Authorization", "Bearer xxx"}};
  remote_server.enabled = true;
  config.mcp_servers.push_back(remote_server);

  // 保存到临时文件
  auto tmp_path = fs::temp_directory_path() / "test_mcp_config.json";
  config.save(tmp_path);

  // 重新加载
  auto loaded = Config::load(tmp_path);

  ASSERT_EQ(loaded.mcp_servers.size(), 2u);

  // 验证本地服务器
  const auto &s0 = loaded.mcp_servers[0];
  EXPECT_EQ(s0.name, "my-server");
  EXPECT_EQ(s0.type, "local");
  EXPECT_EQ(s0.command, "npx");
  ASSERT_EQ(s0.args.size(), 2u);
  EXPECT_EQ(s0.args[0], "-y");
  EXPECT_EQ(s0.args[1], "@modelcontextprotocol/server-filesystem");
  ASSERT_EQ(s0.env.size(), 1u);
  EXPECT_EQ(s0.env.at("HOME"), "/tmp");
  EXPECT_TRUE(s0.enabled);

  // 验证远程服务器
  const auto &s1 = loaded.mcp_servers[1];
  EXPECT_EQ(s1.name, "remote-server");
  EXPECT_EQ(s1.type, "remote");
  EXPECT_EQ(s1.url, "https://example.com/mcp");
  ASSERT_EQ(s1.headers.size(), 1u);
  EXPECT_EQ(s1.headers.at("Authorization"), "Bearer xxx");
  EXPECT_TRUE(s1.enabled);

  // 清理临时文件
  fs::remove(tmp_path);
}

TEST(ConfigTest, SaveAndLoadAgents) {
  Config config;

  // 添加 build 代理
  AgentConfig build_agent;
  build_agent.id = "build";
  build_agent.type = AgentType::Build;
  build_agent.model = "claude-sonnet-4-20250514";
  build_agent.system_prompt = "You are a coding assistant";
  build_agent.max_tokens = 200000;
  build_agent.default_permission = Permission::Allow;
  build_agent.allowed_tools = {"bash", "read"};
  build_agent.denied_tools = {"write"};
  build_agent.permissions = {{"bash", Permission::Ask}};
  config.agents["build"] = build_agent;

  // 添加 explore 代理
  AgentConfig explore_agent;
  explore_agent.id = "explore";
  explore_agent.type = AgentType::Explore;
  explore_agent.model = "gpt-4o";
  explore_agent.system_prompt = "Read-only exploration agent";
  explore_agent.max_tokens = 50000;
  explore_agent.default_permission = Permission::Deny;
  config.agents["explore"] = explore_agent;

  // 保存到临时文件
  auto tmp_path = fs::temp_directory_path() / "test_agents_config.json";
  config.save(tmp_path);

  // 重新加载
  auto loaded = Config::load(tmp_path);

  ASSERT_EQ(loaded.agents.size(), 2u);

  // 验证 build 代理
  auto build_opt = loaded.get_agent("build");
  ASSERT_TRUE(build_opt.has_value());
  const auto &b = *build_opt;
  EXPECT_EQ(b.id, "build");
  EXPECT_EQ(b.type, AgentType::Build);
  EXPECT_EQ(b.model, "claude-sonnet-4-20250514");
  EXPECT_EQ(b.system_prompt, "You are a coding assistant");
  EXPECT_EQ(b.max_tokens, 200000);
  EXPECT_EQ(b.default_permission, Permission::Allow);
  ASSERT_EQ(b.allowed_tools.size(), 2u);
  EXPECT_EQ(b.allowed_tools[0], "bash");
  EXPECT_EQ(b.allowed_tools[1], "read");
  ASSERT_EQ(b.denied_tools.size(), 1u);
  EXPECT_EQ(b.denied_tools[0], "write");
  ASSERT_EQ(b.permissions.size(), 1u);
  EXPECT_EQ(b.permissions.at("bash"), Permission::Ask);

  // 验证 explore 代理
  auto explore_opt = loaded.get_agent("explore");
  ASSERT_TRUE(explore_opt.has_value());
  const auto &e = *explore_opt;
  EXPECT_EQ(e.id, "explore");
  EXPECT_EQ(e.type, AgentType::Explore);
  EXPECT_EQ(e.model, "gpt-4o");
  EXPECT_EQ(e.system_prompt, "Read-only exploration agent");
  EXPECT_EQ(e.max_tokens, 50000);
  EXPECT_EQ(e.default_permission, Permission::Deny);
  EXPECT_TRUE(e.allowed_tools.empty());
  EXPECT_TRUE(e.denied_tools.empty());
  EXPECT_TRUE(e.permissions.empty());

  // 清理临时文件
  fs::remove(tmp_path);
}
