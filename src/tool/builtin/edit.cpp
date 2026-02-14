#include <filesystem>
#include <fstream>
#include <sstream>

#include "builtins.hpp"

namespace agent::tools {

namespace fs = std::filesystem;

// ============================================================================
// EditTool
// ============================================================================

EditTool::EditTool() : SimpleTool("edit", "Performs exact string replacements in files using search and replace.") {}

std::vector<ParameterSchema> EditTool::parameters() const {
  return {{"filePath", "string", "The absolute path to the file to modify", true, std::nullopt, std::nullopt},
          {"oldString", "string", "The text to replace", true, std::nullopt, std::nullopt},
          {"newString", "string", "The text to replace it with", true, std::nullopt, std::nullopt},
          {"replaceAll", "boolean", "Replace all occurrences (default false)", false, json(false), std::nullopt}};
}

std::future<ToolResult> EditTool::execute(const json& args, const ToolContext& ctx) {
  return std::async(std::launch::async, [args, ctx]() -> ToolResult {
    std::string file_path = args.value("filePath", "");
    std::string old_str = args.value("oldString", "");
    std::string new_str = args.value("newString", "");
    bool replace_all = args.value("replaceAll", false);

    if (file_path.empty()) {
      return ToolResult::error("filePath is required");
    }
    if (old_str.empty()) {
      return ToolResult::error("oldString is required");
    }

    fs::path path = file_path;
    if (!path.is_absolute()) {
      path = fs::path(ctx.working_dir) / path;
    }

    if (!fs::exists(path)) {
      return ToolResult::error("File not found: " + path.string());
    }

    // Read file content
    std::ifstream file(path);
    if (!file.is_open()) {
      return ToolResult::error("Failed to open file: " + path.string());
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    file.close();

    // Find occurrences
    size_t count = 0;
    size_t pos = 0;
    while ((pos = content.find(old_str, pos)) != std::string::npos) {
      count++;
      pos += old_str.length();
    }

    if (count == 0) {
      return ToolResult::error("oldString not found in content");
    }

    if (count > 1 && !replace_all) {
      return ToolResult::error("oldString found " + std::to_string(count) + " times. " +
                               "Use replaceAll=true to replace all occurrences, or provide more context to make it unique.");
    }

    // Perform replacement
    std::string new_content;
    pos = 0;
    size_t replaced = 0;

    while (true) {
      size_t found = content.find(old_str, pos);
      if (found == std::string::npos) {
        new_content += content.substr(pos);
        break;
      }

      new_content += content.substr(pos, found - pos);
      new_content += new_str;
      pos = found + old_str.length();
      replaced++;

      if (!replace_all) break;
    }

    if (!replace_all && pos < content.length()) {
      new_content += content.substr(pos);
    }

    // Write back
    std::ofstream out_file(path);
    if (!out_file.is_open()) {
      return ToolResult::error("Failed to write file: " + path.string());
    }

    out_file << new_content;
    out_file.close();

    return ToolResult::with_title("Replaced " + std::to_string(replaced) + " occurrence(s) in " + path.string(),
                                  "Edited " + path.filename().string());
  });
}

}  // namespace agent::tools
