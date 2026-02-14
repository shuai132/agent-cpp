#pragma once

#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace agent::skill {

// Parsed SKILL.md representation
struct SkillInfo {
  std::string name;                             // Required: lowercase alphanumeric with hyphens
  std::string description;                      // Required: 1-1024 chars
  std::string body;                             // Markdown content after frontmatter
  std::optional<std::string> license;           // Optional
  std::optional<std::string> compatibility;     // Optional
  std::map<std::string, std::string> metadata;  // Optional: string-to-string map
  std::filesystem::path source_path;            // Absolute path to the SKILL.md file
};

// Result of parsing a SKILL.md file
struct ParseResult {
  std::optional<SkillInfo> skill;
  std::optional<std::string> error;

  bool ok() const {
    return skill.has_value();
  }
};

// Validate a skill name according to the spec:
//   - 1-64 characters
//   - lowercase alphanumeric with single hyphen separators
//   - No leading/trailing hyphens, no consecutive hyphens
//   - Must match: ^[a-z0-9]+(-[a-z0-9]+)*$
bool validate_skill_name(const std::string &name);

// Parse a SKILL.md file
ParseResult parse_skill_file(const std::filesystem::path &path);

// Skill registry — singleton that discovers and stores available skills
class SkillRegistry {
 public:
  static SkillRegistry &instance();

  // Discover skills from all standard locations:
  //   Project-local (traversing up from start_dir to git root):
  //     .agent-sdk/skills/*/SKILL.md
  //     .agents/skills/*/SKILL.md
  //     .claude/skills/*/SKILL.md
  //     .opencode/skills/*/SKILL.md
  //   Global:
  //     ~/.config/agent-sdk/skills/*/SKILL.md
  //     ~/.agents/skills/*/SKILL.md
  //     ~/.claude/skills/*/SKILL.md
  //     ~/.config/opencode/skills/*/SKILL.md
  //   Additional custom paths from config
  void discover(const std::filesystem::path &start_dir, const std::vector<std::filesystem::path> &extra_paths = {});

  // Get a skill by name
  std::optional<SkillInfo> get(const std::string &name) const;

  // Get all discovered skills
  std::vector<SkillInfo> all() const;

  // Get the number of registered skills
  size_t size() const;

  // Clear all registered skills
  void clear();

 private:
  SkillRegistry() = default;

  // Scan a single directory for skills/*/SKILL.md
  void scan_skills_dir(const std::filesystem::path &skills_dir);

  // Register a skill (first-wins dedup by name)
  void register_skill(SkillInfo skill);

  mutable std::mutex mutex_;
  std::map<std::string, SkillInfo> skills_;
};

}  // namespace agent::skill
