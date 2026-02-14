// Tool registry initialization - delegates to tools::register_builtins() in tool/builtin/bash.cpp
#include "builtin/builtins.hpp"
#include "tool.hpp"

namespace agent {

void ToolRegistry::init_builtins() {
  tools::register_builtins();
}

}  // namespace agent
