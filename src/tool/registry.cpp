// Tool registry initialization - delegates to tools::register_builtins() in tool/builtin/bash.cpp
#include "tool/builtin/builtins.hpp"
#include "tool/tool.hpp"

namespace agent {

void ToolRegistry::init_builtins() {
  tools::register_builtins();
}

}  // namespace agent
