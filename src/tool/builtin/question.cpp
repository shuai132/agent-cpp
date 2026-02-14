#include <spdlog/spdlog.h>

#include <sstream>

#include "builtins.hpp"

namespace agent::tools {

// ============================================================================
// QuestionTool
// ============================================================================

QuestionTool::QuestionTool() : SimpleTool("question", "Ask the user a question to gather information or clarify requirements.") {}

std::vector<ParameterSchema> QuestionTool::parameters() const {
  return {{"questions", "array", "Array of questions to ask the user (strings)", true, std::nullopt, std::nullopt}};
}

std::future<ToolResult> QuestionTool::execute(const json& args, const ToolContext& ctx) {
  return std::async(std::launch::async, [args, &ctx]() -> ToolResult {
    auto questions_json = args.value("questions", json::array());

    // Extract question strings
    std::vector<std::string> questions;
    for (const auto& q : questions_json) {
      if (q.is_string()) {
        questions.push_back(q.get<std::string>());
      } else if (q.is_object() && q.contains("question")) {
        questions.push_back(q["question"].get<std::string>());
      }
    }

    if (questions.empty()) {
      return ToolResult::error("No questions provided");
    }

    // Check if question_handler is available
    if (!ctx.question_handler) {
      // Fallback: return questions as formatted text (non-interactive mode)
      spdlog::warn("question tool: no question_handler available, returning questions as text");
      std::ostringstream output;
      output << "Questions for user (no interactive handler available):\n";
      for (size_t i = 0; i < questions.size(); i++) {
        output << "\n" << (i + 1) << ". " << questions[i];
      }
      return ToolResult::error(output.str());
    }

    // Use question_handler to interact with user
    QuestionInfo info;
    info.questions = questions;

    try {
      auto future = ctx.question_handler(info);
      auto response = future.get();

      if (response.cancelled) {
        return ToolResult::error("User cancelled the question");
      }

      // Format the response
      std::ostringstream output;
      output << "User responses:\n";
      for (size_t i = 0; i < questions.size() && i < response.answers.size(); i++) {
        output << "\nQ" << (i + 1) << ": " << questions[i];
        output << "\nA" << (i + 1) << ": " << response.answers[i] << "\n";
      }

      return ToolResult::success(output.str());
    } catch (const std::exception& e) {
      spdlog::error("question tool: handler error: {}", e.what());
      return ToolResult::error(std::string("Failed to get user response: ") + e.what());
    }
  });
}

}  // namespace agent::tools
