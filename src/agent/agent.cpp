// Agent initialization
#include "agent/agent.hpp"

#include <filesystem>

#include "llm/anthropic.hpp"
#include "skill/skill.hpp"
#include "tool/builtin/builtins.hpp"

namespace agent {

// Force inclusion of Anthropic provider registration
namespace {
// This function exists to force the linker to include the anthropic.cpp
// translation unit which contains the static provider registration
void force_provider_registration() {
  // Just reference the type to ensure the translation unit is linked
  (void)sizeof(llm::AnthropicProvider);
}
}  // namespace

void init() {
  force_provider_registration();
  tools::register_builtins();

  // Discover skills from current working directory and standard locations
  auto cwd = std::filesystem::current_path();
  auto config = Config::load_default();
  skill::SkillRegistry::instance().discover(cwd, config.skill_paths);
}

void shutdown() {
  // Cleanup if needed
}

std::string version() {
  return "0.1.0";
}

}  // namespace agent
