#include <gtest/gtest.h>

#include "net/http_client.hpp"

using namespace agent::net;

// ============================================================
// ParsedUrl 解析测试
// ============================================================

TEST(ParsedUrlTest, ParseHttpUrl) {
  auto result = ParsedUrl::parse("http://example.com/path");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->scheme, "http");
  EXPECT_EQ(result->host, "example.com");
  EXPECT_EQ(result->path, "/path");
  EXPECT_TRUE(result->port.empty());
}

TEST(ParsedUrlTest, ParseHttpsUrl) {
  auto result = ParsedUrl::parse("https://api.example.com/v1/chat");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->scheme, "https");
  EXPECT_EQ(result->host, "api.example.com");
  EXPECT_EQ(result->path, "/v1/chat");
}

TEST(ParsedUrlTest, ParseUrlWithPort) {
  auto result = ParsedUrl::parse("http://localhost:8080/api");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->scheme, "http");
  EXPECT_EQ(result->host, "localhost");
  EXPECT_EQ(result->port, "8080");
  EXPECT_EQ(result->path, "/api");
}

TEST(ParsedUrlTest, ParseUrlWithQuery) {
  auto result = ParsedUrl::parse("https://example.com/search?q=test&page=1");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->scheme, "https");
  EXPECT_EQ(result->host, "example.com");
  EXPECT_EQ(result->path, "/search");
  EXPECT_EQ(result->query, "?q=test&page=1");
}

TEST(ParsedUrlTest, ParseUrlWithDefaultPort) {
  auto https_result = ParsedUrl::parse("https://example.com/path");
  ASSERT_TRUE(https_result.has_value());
  EXPECT_EQ(https_result->port_or_default(), "443");

  auto http_result = ParsedUrl::parse("http://example.com/path");
  ASSERT_TRUE(http_result.has_value());
  EXPECT_EQ(http_result->port_or_default(), "80");
}

TEST(ParsedUrlTest, IsHttps) {
  auto https_result = ParsedUrl::parse("https://example.com/path");
  ASSERT_TRUE(https_result.has_value());
  EXPECT_TRUE(https_result->is_https());

  auto http_result = ParsedUrl::parse("http://example.com/path");
  ASSERT_TRUE(http_result.has_value());
  EXPECT_FALSE(http_result->is_https());
}

TEST(ParsedUrlTest, InvalidUrl) {
  auto empty = ParsedUrl::parse("");
  EXPECT_FALSE(empty.has_value());

  auto no_scheme = ParsedUrl::parse("not-a-url");
  EXPECT_FALSE(no_scheme.has_value());
}

TEST(ParsedUrlTest, ParseUrlNoPath) {
  auto result = ParsedUrl::parse("https://example.com");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->scheme, "https");
  EXPECT_EQ(result->host, "example.com");
  EXPECT_EQ(result->path, "/");
}

// ============================================================
// HttpResponse 测试
// ============================================================

TEST(HttpResponseTest, OkStatusCodes) {
  HttpResponse resp200;
  resp200.status_code = 200;
  EXPECT_TRUE(resp200.ok());

  HttpResponse resp201;
  resp201.status_code = 201;
  EXPECT_TRUE(resp201.ok());

  HttpResponse resp299;
  resp299.status_code = 299;
  EXPECT_TRUE(resp299.ok());
}

TEST(HttpResponseTest, ErrorStatusCodes) {
  HttpResponse resp0;
  resp0.status_code = 0;
  EXPECT_FALSE(resp0.ok());

  HttpResponse resp199;
  resp199.status_code = 199;
  EXPECT_FALSE(resp199.ok());

  HttpResponse resp300;
  resp300.status_code = 300;
  EXPECT_FALSE(resp300.ok());

  HttpResponse resp404;
  resp404.status_code = 404;
  EXPECT_FALSE(resp404.ok());

  HttpResponse resp500;
  resp500.status_code = 500;
  EXPECT_FALSE(resp500.ok());
}
