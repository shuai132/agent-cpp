#include <algorithm>
#include <filesystem>
#include <sstream>

#include "tool/builtin/builtins.hpp"

namespace agent::tools {

namespace fs = std::filesystem;

// ============================================================================
// Glob matching — helper functions
// ============================================================================

// Expand brace patterns like {a,b,c} into multiple strings.
// Supports nesting: {a,b{c,d}} → a, bc, bd
static std::vector<std::string> expand_braces(const std::string &pattern) {
  // Find the first top-level '{'
  size_t open_pos = std::string::npos;
  for (size_t i = 0; i < pattern.size(); ++i) {
    if (pattern[i] == '{') {
      open_pos = i;
      break;
    }
  }

  if (open_pos == std::string::npos) {
    return {pattern};
  }

  // Find the matching closing brace (respecting nesting)
  int depth = 0;
  size_t close_pos = std::string::npos;
  for (size_t i = open_pos; i < pattern.size(); ++i) {
    if (pattern[i] == '{') {
      depth++;
    } else if (pattern[i] == '}') {
      depth--;
      if (depth == 0) {
        close_pos = i;
        break;
      }
    }
  }

  if (close_pos == std::string::npos) {
    return {pattern};
  }

  std::string prefix = pattern.substr(0, open_pos);
  std::string suffix = pattern.substr(close_pos + 1);
  std::string inner = pattern.substr(open_pos + 1, close_pos - open_pos - 1);

  // Split inner by top-level commas (respecting nested braces)
  std::vector<std::string> alternatives;
  depth = 0;
  size_t start = 0;
  for (size_t i = 0; i < inner.size(); ++i) {
    if (inner[i] == '{') {
      depth++;
    } else if (inner[i] == '}') {
      depth--;
    } else if (inner[i] == ',' && depth == 0) {
      alternatives.push_back(inner.substr(start, i - start));
      start = i + 1;
    }
  }
  alternatives.push_back(inner.substr(start));

  // Combine prefix + each alternative + suffix, then recursively expand
  std::vector<std::string> results;
  for (const auto &alt : alternatives) {
    auto expanded = expand_braces(prefix + alt + suffix);
    results.insert(results.end(), expanded.begin(), expanded.end());
  }
  return results;
}

// Match a single glob segment (no path separators) against a string.
// Supports: * (any chars), ? (single char), [abc], [^abc]/[!abc], [a-z] ranges
static bool match_segment(const std::string &pattern, size_t pi, const std::string &str, size_t si) {
  while (pi < pattern.size() && si < str.size()) {
    char pc = pattern[pi];

    if (pc == '*') {
      pi++;
      for (size_t k = si; k <= str.size(); ++k) {
        if (match_segment(pattern, pi, str, k)) {
          return true;
        }
      }
      return false;
    } else if (pc == '?') {
      pi++;
      si++;
    } else if (pc == '[') {
      pi++;
      bool negated = false;
      if (pi < pattern.size() && (pattern[pi] == '!' || pattern[pi] == '^')) {
        negated = true;
        pi++;
      }
      bool found = false;
      while (pi < pattern.size() && pattern[pi] != ']') {
        if (pi + 2 < pattern.size() && pattern[pi + 1] == '-' && pattern[pi + 2] != ']') {
          char lo = pattern[pi];
          char hi = pattern[pi + 2];
          if (str[si] >= lo && str[si] <= hi) {
            found = true;
          }
          pi += 3;
        } else {
          if (pattern[pi] == str[si]) {
            found = true;
          }
          pi++;
        }
      }
      if (pi < pattern.size()) pi++;       // skip ']'
      if (found == negated) return false;  // negated XOR found must be true
      si++;
    } else {
      if (pc != str[si]) return false;
      pi++;
      si++;
    }
  }

  // Skip trailing '*' in pattern
  while (pi < pattern.size() && pattern[pi] == '*') pi++;

  return pi == pattern.size() && si == str.size();
}

static bool match_segment(const std::string &pattern, const std::string &str) {
  return match_segment(pattern, 0, str, 0);
}

// Split a path string by '/' into segments
static std::vector<std::string> split_path(const std::string &path) {
  std::vector<std::string> segments;
  std::istringstream iss(path);
  std::string seg;
  while (std::getline(iss, seg, '/')) {
    if (!seg.empty()) {
      segments.push_back(seg);
    }
  }
  return segments;
}

// Match a full relative path against a glob pattern (with ** support).
static bool match_glob_path(const std::vector<std::string> &pat_segs, size_t pi, const std::vector<std::string> &path_segs, size_t si) {
  while (pi < pat_segs.size() && si < path_segs.size()) {
    if (pat_segs[pi] == "**") {
      pi++;
      if (pi == pat_segs.size()) return true;
      for (size_t k = si; k <= path_segs.size(); ++k) {
        if (match_glob_path(pat_segs, pi, path_segs, k)) {
          return true;
        }
      }
      return false;
    } else {
      if (!match_segment(pat_segs[pi], path_segs[si])) {
        return false;
      }
      pi++;
      si++;
    }
  }

  // Skip trailing '**'
  while (pi < pat_segs.size() && pat_segs[pi] == "**") pi++;

  return pi == pat_segs.size() && si == path_segs.size();
}

static bool match_glob(const std::string &pattern, const std::string &rel_path) {
  auto pat_segs = split_path(pattern);
  auto path_segs = split_path(rel_path);
  return match_glob_path(pat_segs, 0, path_segs, 0);
}

// ============================================================================
// GlobTool
// ============================================================================

GlobTool::GlobTool() : SimpleTool("glob", "Fast file pattern matching tool. Supports glob patterns like \"**/*.js\".") {}

std::vector<ParameterSchema> GlobTool::parameters() const {
  return {{"pattern", "string", "The glob pattern to match files against", true, std::nullopt, std::nullopt},
          {"path", "string", "The directory to search in", false, std::nullopt, std::nullopt}};
}

std::future<ToolResult> GlobTool::execute(const json &args, const ToolContext &ctx) {
  return std::async(std::launch::async, [args, ctx]() -> ToolResult {
    std::string pattern = args.value("pattern", "");
    std::string search_path = args.value("path", ctx.working_dir);

    if (pattern.empty()) {
      return ToolResult::error("pattern is required");
    }

    fs::path base_path = search_path;
    if (!base_path.is_absolute()) {
      base_path = fs::path(ctx.working_dir) / base_path;
    }

    if (!fs::exists(base_path)) {
      return ToolResult::error("Path not found: " + base_path.string());
    }

    // Expand brace patterns (e.g. *.{cpp,hpp} → *.cpp, *.hpp)
    auto expanded_patterns = expand_braces(pattern);

    std::vector<std::string> matches;

    try {
      for (const auto &entry : fs::recursive_directory_iterator(base_path)) {
        if (!entry.is_regular_file()) continue;

        std::string rel_path = fs::relative(entry.path(), base_path).string();

        for (const auto &pat : expanded_patterns) {
          bool has_path_sep = (pat.find('/') != std::string::npos);

          if (has_path_sep) {
            if (match_glob(pat, rel_path)) {
              matches.push_back(rel_path);
              break;
            }
          } else {
            std::string filename = entry.path().filename().string();
            if (match_segment(pat, filename)) {
              matches.push_back(rel_path);
              break;
            }
          }
        }
      }
    } catch (const std::exception &e) {
      return ToolResult::error(std::string("Error searching: ") + e.what());
    }

    if (matches.empty()) {
      return ToolResult::success("No files found matching pattern: " + pattern);
    }

    std::sort(matches.begin(), matches.end());

    std::ostringstream output;
    for (const auto &match : matches) {
      output << match << "\n";
    }

    return ToolResult::with_title(output.str(), "Found " + std::to_string(matches.size()) + " files");
  });
}

}  // namespace agent::tools
