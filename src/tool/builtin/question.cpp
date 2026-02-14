#include <sstream>

#include "tool/builtin/builtins.hpp"

namespace agent::tools {

// ============================================================================
// QuestionTool
// ============================================================================

QuestionTool::QuestionTool() : SimpleTool("question", "Ask the user a question to gather information or clarify requirements.") {}

std::vector<ParameterSchema> QuestionTool::parameters() const {
  return {{"questions", "array", "Array of questions to ask", true, std::nullopt, std::nullopt}};
}

std::future<ToolResult> QuestionTool::execute(const json &args, const ToolContext &ctx) {
  // This would typically be handled by the session to interact with the user
  return std::async(std::launch::async, [args]() -> ToolResult {
    auto questions = args.value("questions", json::array());

    std::ostringstream output;
    output << "Questions for user:\n";

    for (size_t i = 0; i < questions.size(); i++) {
      auto q = questions[i];
      output << "\n" << (i + 1) << ". " << q.value("question", "") << "\n";

      if (q.contains("options")) {
        for (const auto &opt : q["options"]) {
          output << "   - " << opt.value("label", "") << ": " << opt.value("description", "") << "\n";
        }
      }
    }

    return ToolResult::with_title(output.str(), "Waiting for user response");
  });
}

}  // namespace agent::tools
