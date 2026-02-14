#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

#include "builtins.hpp"

namespace agent::tools {

namespace fs = std::filesystem;

// ============================================================================
// GrepTool
// ============================================================================

GrepTool::GrepTool() : SimpleTool("grep", "Fast content search tool. Searches file contents using regular expressions.") {}

std::vector<ParameterSchema> GrepTool::parameters() const {
  return {{"pattern", "string", "The regex pattern to search for", true, std::nullopt, std::nullopt},
          {"path", "string", "The directory to search in", false, std::nullopt, std::nullopt},
          {"include", "string", "File pattern to include (e.g. \"*.js\")", false, std::nullopt, std::nullopt}};
}

std::future<ToolResult> GrepTool::execute(const json& args, const ToolContext& ctx) {
  return std::async(std::launch::async, [args, ctx]() -> ToolResult {
    std::string pattern = args.value("pattern", "");
    std::string search_path = args.value("path", ctx.working_dir);
    std::string include = args.value("include", "");

    if (pattern.empty()) {
      return ToolResult::error("pattern is required");
    }

    fs::path base_path = search_path;
    if (!base_path.is_absolute()) {
      base_path = fs::path(ctx.working_dir) / base_path;
    }

    std::regex search_regex;
    try {
      search_regex = std::regex(pattern);
    } catch (const std::regex_error& e) {
      return ToolResult::error("Invalid regex pattern: " + std::string(e.what()));
    }

    std::ostringstream output;
    size_t match_count = 0;
    const size_t max_matches = 100;

    try {
      for (const auto& entry : fs::recursive_directory_iterator(base_path)) {
        if (!entry.is_regular_file()) continue;
        if (match_count >= max_matches) break;

        std::string filename = entry.path().filename().string();

        // Check include pattern
        if (!include.empty()) {
          bool matches_include = false;
          if (include.find('*') != std::string::npos) {
            std::string ext = include.substr(include.rfind('.'));
            if (filename.size() >= ext.size() && filename.compare(filename.size() - ext.size(), ext.size(), ext) == 0) {
              matches_include = true;
            }
          } else {
            matches_include = (filename == include);
          }
          if (!matches_include) continue;
        }

        std::ifstream file(entry.path());
        if (!file.is_open()) continue;

        std::string line;
        int line_num = 0;
        std::string rel_path = fs::relative(entry.path(), base_path).string();

        while (std::getline(file, line) && match_count < max_matches) {
          line_num++;
          if (std::regex_search(line, search_regex)) {
            output << rel_path << ":" << line_num << ": " << line << "\n";
            match_count++;
          }
        }
      }
    } catch (const std::exception& e) {
      return ToolResult::error(std::string("Error searching: ") + e.what());
    }

    if (match_count == 0) {
      return ToolResult::success("No matches found for pattern: " + pattern);
    }

    std::string result = output.str();
    if (match_count >= max_matches) {
      result += "\n... (results truncated, showing first " + std::to_string(max_matches) + " matches)";
    }

    return ToolResult::with_title(result, std::to_string(match_count) + " matches");
  });
}

}  // namespace agent::tools
