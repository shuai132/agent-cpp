#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "builtins.hpp"

namespace agent::tools {

namespace fs = std::filesystem;

// ============================================================================
// ReadTool
// ============================================================================

ReadTool::ReadTool() : SimpleTool("read", "Reads a file from the local filesystem. Returns the file content with line numbers.") {}

std::vector<ParameterSchema> ReadTool::parameters() const {
  return {{"filePath", "string", "The absolute path to the file to read", true, std::nullopt, std::nullopt},
          {"offset", "number", "The line number to start reading from (0-based)", false, json(0), std::nullopt},
          {"limit", "number", "The number of lines to read (defaults to 2000)", false, json(2000), std::nullopt}};
}

std::future<ToolResult> ReadTool::execute(const json& args, const ToolContext& ctx) {
  return std::async(std::launch::async, [args, ctx]() -> ToolResult {
    std::string file_path = args.value("filePath", "");
    int offset = args.value("offset", 0);
    int limit = args.value("limit", 2000);

    if (file_path.empty()) {
      return ToolResult::error("filePath is required");
    }

    // Resolve relative paths
    fs::path path = file_path;
    if (!path.is_absolute()) {
      path = fs::path(ctx.working_dir) / path;
    }

    if (!fs::exists(path)) {
      return ToolResult::error("File not found: " + path.string());
    }

    if (fs::is_directory(path)) {
      return ToolResult::error("Path is a directory, not a file: " + path.string());
    }

    std::ifstream file(path);
    if (!file.is_open()) {
      return ToolResult::error("Failed to open file: " + path.string());
    }

    std::ostringstream output;
    std::string line;
    int line_num = 0;
    int lines_read = 0;

    while (std::getline(file, line)) {
      line_num++;

      if (line_num <= offset) continue;
      if (lines_read >= limit) break;

      // Format with line numbers (similar to cat -n)
      output << std::setw(5) << line_num << "\t" << line << "\n";
      lines_read++;
    }

    std::string content = output.str();

    // Check if file was truncated
    bool has_more = false;
    if (std::getline(file, line)) {
      has_more = true;
    }

    if (has_more) {
      content += "\n(File has more lines. Use 'offset' parameter to read beyond line " + std::to_string(offset + limit) + ")";
    }

    return ToolResult::with_title(content, path.filename().string());
  });
}

}  // namespace agent::tools
