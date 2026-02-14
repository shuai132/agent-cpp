#include <filesystem>
#include <fstream>

#include "builtins.hpp"

namespace agent::tools {

namespace fs = std::filesystem;

// ============================================================================
// WriteTool
// ============================================================================

WriteTool::WriteTool() : SimpleTool("write", "Writes content to a file. Creates the file if it doesn't exist, overwrites if it does.") {}

std::vector<ParameterSchema> WriteTool::parameters() const {
  return {{"filePath", "string", "The absolute path to the file to write", true, std::nullopt, std::nullopt},
          {"content", "string", "The content to write to the file", true, std::nullopt, std::nullopt}};
}

std::future<ToolResult> WriteTool::execute(const json& args, const ToolContext& ctx) {
  return std::async(std::launch::async, [args, ctx]() -> ToolResult {
    std::string file_path = args.value("filePath", "");
    std::string content = args.value("content", "");

    if (file_path.empty()) {
      return ToolResult::error("filePath is required");
    }

    fs::path path = file_path;
    if (!path.is_absolute()) {
      path = fs::path(ctx.working_dir) / path;
    }

    // Create parent directories if needed
    auto parent = path.parent_path();
    if (!parent.empty() && !fs::exists(parent)) {
      fs::create_directories(parent);
    }

    std::ofstream file(path);
    if (!file.is_open()) {
      return ToolResult::error("Failed to open file for writing: " + path.string());
    }

    file << content;
    file.close();

    return ToolResult::with_title("Successfully wrote " + std::to_string(content.size()) + " bytes to " + path.string(),
                                  "Wrote " + path.filename().string());
  });
}

}  // namespace agent::tools
