#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>

#include "tool/builtin/builtins.hpp"
#include "tool/tool.hpp"

using namespace agent;
using namespace agent::tools;
namespace fs = std::filesystem;

// Helper: create a ToolContext with a given working directory
static ToolContext make_context(const std::string& working_dir) {
  ToolContext ctx;
  ctx.session_id = "test-session";
  ctx.message_id = "test-message";
  ctx.working_dir = working_dir;
  ctx.abort_signal = std::make_shared<std::atomic<bool>>(false);
  return ctx;
}

// Helper: create a unique temporary directory for each test
class TempDir {
 public:
  TempDir() {
    path_ = fs::temp_directory_path() / ("agent_test_" + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + "_" +
                                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(path_);
  }

  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }

  const fs::path& path() const {
    return path_;
  }

  std::string str() const {
    return path_.string();
  }

  // Create a file with content under this temp dir
  fs::path create_file(const std::string& relative_path, const std::string& content) {
    fs::path full = path_ / relative_path;
    fs::create_directories(full.parent_path());
    std::ofstream ofs(full);
    ofs << content;
    ofs.close();
    return full;
  }

 private:
  fs::path path_;
};

// ============================================================================
// BashToolTest
// ============================================================================

class BashToolTest : public ::testing::Test {
 protected:
  BashTool tool_;
  TempDir tmp_;
};

TEST_F(BashToolTest, EchoCommand) {
  auto ctx = make_context(tmp_.str());
  json args = {{"command", "echo hello"}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_FALSE(result.is_error);
  // 输出应包含 "hello"（popen 捕获 stdout）
  EXPECT_NE(result.output.find("hello"), std::string::npos);
}

TEST_F(BashToolTest, NonexistentCommand) {
  auto ctx = make_context(tmp_.str());
  json args = {{"command", "this_command_does_not_exist_xyz_12345"}};

  auto result = tool_.execute(args, ctx).get();

  // 不存在的命令会返回非零退出码
  EXPECT_TRUE(result.is_error);
}

TEST_F(BashToolTest, WorkingDirectory) {
  // 创建子目录
  fs::path subdir = tmp_.path() / "subdir";
  fs::create_directories(subdir);

  auto ctx = make_context(tmp_.str());
  json args = {{"command", "pwd"}, {"workdir", subdir.string()}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_FALSE(result.is_error);
  // 输出应包含子目录路径
  EXPECT_NE(result.output.find("subdir"), std::string::npos);
}

// ============================================================================
// ReadToolTest
// ============================================================================

class ReadToolTest : public ::testing::Test {
 protected:
  ReadTool tool_;
  TempDir tmp_;
};

TEST_F(ReadToolTest, ReadFile) {
  auto file = tmp_.create_file("hello.txt", "line1\nline2\nline3\n");
  auto ctx = make_context(tmp_.str());
  json args = {{"filePath", file.string()}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_FALSE(result.is_error);
  // ReadTool 输出带行号格式（setw(5) + tab + content）
  EXPECT_NE(result.output.find("line1"), std::string::npos);
  EXPECT_NE(result.output.find("line2"), std::string::npos);
  EXPECT_NE(result.output.find("line3"), std::string::npos);
}

TEST_F(ReadToolTest, ReadWithOffset) {
  // 创建包含 5 行的文件
  auto file = tmp_.create_file("lines.txt", "aaa\nbbb\nccc\nddd\neee\n");
  auto ctx = make_context(tmp_.str());
  // offset=2 跳过前 2 行，limit=2 只读 2 行（第 3、4 行）
  json args = {{"filePath", file.string()}, {"offset", 2}, {"limit", 2}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_FALSE(result.is_error);
  // 应包含第 3、4 行
  EXPECT_NE(result.output.find("ccc"), std::string::npos);
  EXPECT_NE(result.output.find("ddd"), std::string::npos);
  // 不应包含第 1、2 行
  EXPECT_EQ(result.output.find("aaa"), std::string::npos);
  EXPECT_EQ(result.output.find("bbb"), std::string::npos);
}

TEST_F(ReadToolTest, ReadNonexistentFile) {
  auto ctx = make_context(tmp_.str());
  json args = {{"filePath", (tmp_.path() / "nonexistent.txt").string()}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_TRUE(result.is_error);
  EXPECT_NE(result.output.find("not found"), std::string::npos);
}

TEST_F(ReadToolTest, ReadDirectory) {
  // ReadTool 对目录返回错误
  auto ctx = make_context(tmp_.str());
  json args = {{"filePath", tmp_.str()}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_TRUE(result.is_error);
  EXPECT_NE(result.output.find("directory"), std::string::npos);
}

// ============================================================================
// WriteToolTest
// ============================================================================

class WriteToolTest : public ::testing::Test {
 protected:
  WriteTool tool_;
  TempDir tmp_;
};

TEST_F(WriteToolTest, WriteNewFile) {
  fs::path file = tmp_.path() / "new_file.txt";
  auto ctx = make_context(tmp_.str());
  json args = {{"filePath", file.string()}, {"content", "hello world"}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_FALSE(result.is_error);
  // 验证文件确实被写入
  ASSERT_TRUE(fs::exists(file));
  std::ifstream ifs(file);
  std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  EXPECT_EQ(content, "hello world");
}

TEST_F(WriteToolTest, OverwriteFile) {
  auto file = tmp_.create_file("existing.txt", "old content");
  auto ctx = make_context(tmp_.str());
  json args = {{"filePath", file.string()}, {"content", "new content"}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_FALSE(result.is_error);
  std::ifstream ifs(file);
  std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  EXPECT_EQ(content, "new content");
}

TEST_F(WriteToolTest, CreateDirectories) {
  // 写入到多层嵌套的不存在的目录
  fs::path file = tmp_.path() / "a" / "b" / "c" / "deep.txt";
  auto ctx = make_context(tmp_.str());
  json args = {{"filePath", file.string()}, {"content", "deep content"}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_FALSE(result.is_error);
  ASSERT_TRUE(fs::exists(file));
  std::ifstream ifs(file);
  std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  EXPECT_EQ(content, "deep content");
}

// ============================================================================
// EditToolTest
// ============================================================================

class EditToolTest : public ::testing::Test {
 protected:
  EditTool tool_;
  TempDir tmp_;
};

TEST_F(EditToolTest, SearchReplace) {
  auto file = tmp_.create_file("edit_me.txt", "foo bar baz");
  auto ctx = make_context(tmp_.str());
  json args = {{"filePath", file.string()}, {"oldString", "bar"}, {"newString", "qux"}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_FALSE(result.is_error);
  // 验证文件内容
  std::ifstream ifs(file);
  std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  EXPECT_EQ(content, "foo qux baz");
}

TEST_F(EditToolTest, ReplaceAll) {
  auto file = tmp_.create_file("multi.txt", "aaa bbb aaa ccc aaa");
  auto ctx = make_context(tmp_.str());
  json args = {{"filePath", file.string()}, {"oldString", "aaa"}, {"newString", "xxx"}, {"replaceAll", true}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_FALSE(result.is_error);
  std::ifstream ifs(file);
  std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  EXPECT_EQ(content, "xxx bbb xxx ccc xxx");
}

TEST_F(EditToolTest, OldStringNotFound) {
  auto file = tmp_.create_file("no_match.txt", "hello world");
  auto ctx = make_context(tmp_.str());
  json args = {{"filePath", file.string()}, {"oldString", "nonexistent"}, {"newString", "replaced"}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_TRUE(result.is_error);
  EXPECT_NE(result.output.find("not found"), std::string::npos);
}

// ============================================================================
// GlobToolTest
// ============================================================================

class GlobToolTest : public ::testing::Test {
 protected:
  GlobTool tool_;
  TempDir tmp_;
};

TEST_F(GlobToolTest, FindFiles) {
  tmp_.create_file("src/main.cpp", "int main() {}");
  tmp_.create_file("src/util.cpp", "void util() {}");
  tmp_.create_file("src/readme.txt", "readme");

  auto ctx = make_context(tmp_.str());
  json args = {{"pattern", "*.cpp"}, {"path", tmp_.str()}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_FALSE(result.is_error);
  EXPECT_NE(result.output.find("main.cpp"), std::string::npos);
  EXPECT_NE(result.output.find("util.cpp"), std::string::npos);
  // txt 文件不应匹配
  EXPECT_EQ(result.output.find("readme.txt"), std::string::npos);
}

TEST_F(GlobToolTest, NoMatches) {
  tmp_.create_file("file.txt", "content");

  auto ctx = make_context(tmp_.str());
  json args = {{"pattern", "*.xyz"}, {"path", tmp_.str()}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_FALSE(result.is_error);
  // 无匹配时输出提示信息
  EXPECT_NE(result.output.find("No files found"), std::string::npos);
}

TEST_F(GlobToolTest, BraceExpansion) {
  // 花括号展开：*.{cpp,hpp} 应匹配 .cpp 和 .hpp 文件
  tmp_.create_file("src/main.cpp", "int main() {}");
  tmp_.create_file("src/types.hpp", "#pragma once");
  tmp_.create_file("src/readme.txt", "readme");
  tmp_.create_file("src/data.json", "{}");

  auto ctx = make_context(tmp_.str());
  json args = {{"pattern", "*.{cpp,hpp}"}, {"path", tmp_.str()}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_FALSE(result.is_error);
  EXPECT_NE(result.output.find("main.cpp"), std::string::npos);
  EXPECT_NE(result.output.find("types.hpp"), std::string::npos);
  // 其他扩展名不应匹配
  EXPECT_EQ(result.output.find("readme.txt"), std::string::npos);
  EXPECT_EQ(result.output.find("data.json"), std::string::npos);
}

TEST_F(GlobToolTest, DoubleStarDeep) {
  // ** 应匹配多层嵌套目录
  tmp_.create_file("root.txt", "root");
  tmp_.create_file("a/one.txt", "one");
  tmp_.create_file("a/b/two.txt", "two");
  tmp_.create_file("a/b/c/three.txt", "three");
  tmp_.create_file("a/b/c/code.cpp", "code");

  auto ctx = make_context(tmp_.str());
  json args = {{"pattern", "**/*.txt"}, {"path", tmp_.str()}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_FALSE(result.is_error);
  // 应匹配所有 .txt 文件（root.txt 也应被匹配，因为 ** 可匹配零层目录）
  EXPECT_NE(result.output.find("root.txt"), std::string::npos);
  EXPECT_NE(result.output.find("one.txt"), std::string::npos);
  EXPECT_NE(result.output.find("two.txt"), std::string::npos);
  EXPECT_NE(result.output.find("three.txt"), std::string::npos);
  // .cpp 文件不应匹配
  EXPECT_EQ(result.output.find("code.cpp"), std::string::npos);
}

TEST_F(GlobToolTest, DoubleStarWithPrefix) {
  // src/**/*.cpp 只匹配 src 目录下的 .cpp 文件
  tmp_.create_file("src/main.cpp", "main");
  tmp_.create_file("src/core/types.cpp", "types");
  tmp_.create_file("src/core/net/http.cpp", "http");
  tmp_.create_file("lib/other.cpp", "other");

  auto ctx = make_context(tmp_.str());
  json args = {{"pattern", "src/**/*.cpp"}, {"path", tmp_.str()}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_FALSE(result.is_error);
  EXPECT_NE(result.output.find("src/main.cpp"), std::string::npos);
  EXPECT_NE(result.output.find("types.cpp"), std::string::npos);
  EXPECT_NE(result.output.find("http.cpp"), std::string::npos);
  // lib 目录下的不应匹配
  EXPECT_EQ(result.output.find("lib/other.cpp"), std::string::npos);
}

TEST_F(GlobToolTest, CharacterClass) {
  // [ab]*.txt 应匹配以 a 或 b 开头的 .txt 文件
  tmp_.create_file("apple.txt", "apple");
  tmp_.create_file("banana.txt", "banana");
  tmp_.create_file("cherry.txt", "cherry");
  tmp_.create_file("avocado.txt", "avocado");

  auto ctx = make_context(tmp_.str());
  json args = {{"pattern", "[ab]*.txt"}, {"path", tmp_.str()}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_FALSE(result.is_error);
  EXPECT_NE(result.output.find("apple.txt"), std::string::npos);
  EXPECT_NE(result.output.find("banana.txt"), std::string::npos);
  EXPECT_NE(result.output.find("avocado.txt"), std::string::npos);
  // c 开头的不应匹配
  EXPECT_EQ(result.output.find("cherry.txt"), std::string::npos);
}

TEST_F(GlobToolTest, NegatedCharacterClass) {
  // [!a]*.txt 应匹配不以 a 开头的 .txt 文件
  tmp_.create_file("apple.txt", "apple");
  tmp_.create_file("banana.txt", "banana");
  tmp_.create_file("cherry.txt", "cherry");

  auto ctx = make_context(tmp_.str());
  json args = {{"pattern", "[!a]*.txt"}, {"path", tmp_.str()}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_FALSE(result.is_error);
  EXPECT_NE(result.output.find("banana.txt"), std::string::npos);
  EXPECT_NE(result.output.find("cherry.txt"), std::string::npos);
  // a 开头的不应匹配
  EXPECT_EQ(result.output.find("apple.txt"), std::string::npos);
}

TEST_F(GlobToolTest, NestedBraceExpansion) {
  // 嵌套花括号展开：{a,b{c,d}}.txt → a.txt, bc.txt, bd.txt
  tmp_.create_file("a.txt", "a");
  tmp_.create_file("bc.txt", "bc");
  tmp_.create_file("bd.txt", "bd");
  tmp_.create_file("b.txt", "b");
  tmp_.create_file("e.txt", "e");

  auto ctx = make_context(tmp_.str());
  json args = {{"pattern", "{a,b{c,d}}.txt"}, {"path", tmp_.str()}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_FALSE(result.is_error);
  EXPECT_NE(result.output.find("a.txt"), std::string::npos);
  EXPECT_NE(result.output.find("bc.txt"), std::string::npos);
  EXPECT_NE(result.output.find("bd.txt"), std::string::npos);
  // b.txt 和 e.txt 不应匹配
  EXPECT_EQ(result.output.find("b.txt"), std::string::npos);
  EXPECT_EQ(result.output.find("e.txt"), std::string::npos);
}

// ============================================================================
// GrepToolTest
// ============================================================================

class GrepToolTest : public ::testing::Test {
 protected:
  GrepTool tool_;
  TempDir tmp_;
};

TEST_F(GrepToolTest, FindPattern) {
  tmp_.create_file("code.cpp", "int main() {\n  return 0;\n}\n");
  tmp_.create_file("other.txt", "no match here\n");

  auto ctx = make_context(tmp_.str());
  json args = {{"pattern", "main"}, {"path", tmp_.str()}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_FALSE(result.is_error);
  EXPECT_NE(result.output.find("code.cpp"), std::string::npos);
  EXPECT_NE(result.output.find("main"), std::string::npos);
}

TEST_F(GrepToolTest, NoMatches) {
  tmp_.create_file("file.txt", "nothing interesting\n");

  auto ctx = make_context(tmp_.str());
  json args = {{"pattern", "zzzzz_no_match"}, {"path", tmp_.str()}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_FALSE(result.is_error);
  EXPECT_NE(result.output.find("No matches found"), std::string::npos);
}

TEST_F(GrepToolTest, WithIncludeFilter) {
  tmp_.create_file("a.cpp", "hello world\n");
  tmp_.create_file("b.txt", "hello world\n");

  auto ctx = make_context(tmp_.str());
  json args = {{"pattern", "hello"}, {"path", tmp_.str()}, {"include", "*.cpp"}};

  auto result = tool_.execute(args, ctx).get();

  EXPECT_FALSE(result.is_error);
  // 应只匹配 .cpp 文件
  EXPECT_NE(result.output.find("a.cpp"), std::string::npos);
  EXPECT_EQ(result.output.find("b.txt"), std::string::npos);
}
