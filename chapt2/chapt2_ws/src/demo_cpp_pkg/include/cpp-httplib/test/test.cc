#include <httplib.h>
#include <signal.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <type_traits>

#define SERVER_CERT_FILE "./cert.pem"
#define SERVER_CERT2_FILE "./cert2.pem"
#define SERVER_PRIVATE_KEY_FILE "./key.pem"
#define CA_CERT_FILE "./ca-bundle.crt"
#define CLIENT_CA_CERT_FILE "./rootCA.cert.pem"
#define CLIENT_CA_CERT_DIR "."
#define CLIENT_CERT_FILE "./client.cert.pem"
#define CLIENT_PRIVATE_KEY_FILE "./client.key.pem"
#define SERVER_ENCRYPTED_CERT_FILE "./cert_encrypted.pem"
#define SERVER_ENCRYPTED_PRIVATE_KEY_FILE "./key_encrypted.pem"
#define SERVER_ENCRYPTED_PRIVATE_KEY_PASS "test123!"

using namespace std;
using namespace httplib;

const char *HOST = "localhost";
const int PORT = 1234;

const string LONG_QUERY_VALUE = string(25000, '@');
const string LONG_QUERY_URL = "/long-query-value?key=" + LONG_QUERY_VALUE;

const std::string JSON_DATA = "{\"hello\":\"world\"}";

const string LARGE_DATA = string(1024 * 1024 * 100, '@'); // 100MB

MultipartFormData &get_file_value(MultipartFormDataItems &files,
                                  const char *key) {
  auto it = std::find_if(
      files.begin(), files.end(),
      [&](const MultipartFormData &file) { return file.name == key; });
#ifdef CPPHTTPLIB_NO_EXCEPTIONS
  return *it;
#else
  if (it != files.end()) { return *it; }
  throw std::runtime_error("invalid multipart form data name error");
#endif
}

TEST(ConstructorTest, MoveConstructible) {
  EXPECT_FALSE(std::is_copy_constructible<Client>::value);
  EXPECT_TRUE(std::is_nothrow_move_constructible<Client>::value);
}

#ifdef _WIN32
TEST(StartupTest, WSAStartup) {
  WSADATA wsaData;
  int ret = WSAStartup(0x0002, &wsaData);
  ASSERT_EQ(0, ret);
}
#endif

TEST(DecodeURLTest, PercentCharacter) {
  EXPECT_EQ(
      detail::decode_url(
          R"(descrip=Gastos%20%C3%A1%C3%A9%C3%AD%C3%B3%C3%BA%C3%B1%C3%91%206)",
          false),
      R"(descrip=Gastos áéíóúñÑ 6)");
}

TEST(EncodeQueryParamTest, ParseUnescapedChararactersTest) {
  string unescapedCharacters = "-_.!~*'()";

  EXPECT_EQ(detail::encode_query_param(unescapedCharacters), "-_.!~*'()");
}

TEST(EncodeQueryParamTest, ParseReservedCharactersTest) {
  string reservedCharacters = ";,/?:@&=+$";

  EXPECT_EQ(detail::encode_query_param(reservedCharacters),
            "%3B%2C%2F%3F%3A%40%26%3D%2B%24");
}

TEST(EncodeQueryParamTest, TestUTF8Characters) {
  string chineseCharacters = "中国語";
  string russianCharacters = "дом";
  string brazilianCharacters = "óculos";

  EXPECT_EQ(detail::encode_query_param(chineseCharacters),
            "%E4%B8%AD%E5%9B%BD%E8%AA%9E");

  EXPECT_EQ(detail::encode_query_param(russianCharacters),
            "%D0%B4%D0%BE%D0%BC");

  EXPECT_EQ(detail::encode_query_param(brazilianCharacters), "%C3%B3culos");
}

TEST(TrimTests, TrimStringTests) {
  EXPECT_EQ("abc", detail::trim_copy("abc"));
  EXPECT_EQ("abc", detail::trim_copy("  abc  "));
  EXPECT_TRUE(detail::trim_copy("").empty());
}

TEST(SplitTest, ParseQueryString) {
  string s = "key1=val1&key2=val2&key3=val3";
  Params dic;

  detail::split(s.c_str(), s.c_str() + s.size(), '&',
                [&](const char *b, const char *e) {
                  string key, val;
                  detail::split(b, e, '=', [&](const char *b2, const char *e2) {
                    if (key.empty()) {
                      key.assign(b2, e2);
                    } else {
                      val.assign(b2, e2);
                    }
                  });
                  dic.emplace(key, val);
                });

  EXPECT_EQ("val1", dic.find("key1")->second);
  EXPECT_EQ("val2", dic.find("key2")->second);
  EXPECT_EQ("val3", dic.find("key3")->second);
}

TEST(SplitTest, ParseInvalidQueryTests) {

  {
    string s = " ";
    Params dict;
    detail::parse_query_text(s, dict);
    EXPECT_TRUE(dict.empty());
  }

  {
    string s = " = =";
    Params dict;
    detail::parse_query_text(s, dict);
    EXPECT_TRUE(dict.empty());
  }
}

TEST(ParseQueryTest, ParseQueryString) {
  string s = "key1=val1&key2=val2&key3=val3";
  Params dic;

  detail::parse_query_text(s, dic);

  EXPECT_EQ("val1", dic.find("key1")->second);
  EXPECT_EQ("val2", dic.find("key2")->second);
  EXPECT_EQ("val3", dic.find("key3")->second);
}

TEST(ParamsToQueryTest, ConvertParamsToQuery) {
  Params dic;

  EXPECT_EQ(detail::params_to_query_str(dic), "");

  dic.emplace("key1", "val1");

  EXPECT_EQ(detail::params_to_query_str(dic), "key1=val1");

  dic.emplace("key2", "val2");
  dic.emplace("key3", "val3");

  EXPECT_EQ(detail::params_to_query_str(dic), "key1=val1&key2=val2&key3=val3");
}

TEST(ParseMultipartBoundaryTest, DefaultValue) {
  string content_type = "multipart/form-data; boundary=something";
  string boundary;
  auto ret = detail::parse_multipart_boundary(content_type, boundary);
  EXPECT_TRUE(ret);
  EXPECT_EQ(boundary, "something");
}

TEST(ParseMultipartBoundaryTest, ValueWithQuote) {
  string content_type = "multipart/form-data; boundary=\"gc0pJq0M:08jU534c0p\"";
  string boundary;
  auto ret = detail::parse_multipart_boundary(content_type, boundary);
  EXPECT_TRUE(ret);
  EXPECT_EQ(boundary, "gc0pJq0M:08jU534c0p");
}

TEST(ParseMultipartBoundaryTest, ValueWithCharset) {
  string content_type =
      "multipart/mixed; boundary=THIS_STRING_SEPARATES;charset=UTF-8";
  string boundary;
  auto ret = detail::parse_multipart_boundary(content_type, boundary);
  EXPECT_TRUE(ret);
  EXPECT_EQ(boundary, "THIS_STRING_SEPARATES");
}

TEST(ParseMultipartBoundaryTest, ValueWithQuotesAndCharset) {
  string content_type =
      "multipart/mixed; boundary=\"cpp-httplib-multipart-data\"; charset=UTF-8";
  string boundary;
  auto ret = detail::parse_multipart_boundary(content_type, boundary);
  EXPECT_TRUE(ret);
  EXPECT_EQ(boundary, "cpp-httplib-multipart-data");
}

TEST(GetHeaderValueTest, DefaultValue) {
  Headers headers = {{"Dummy", "Dummy"}};
  auto val = detail::get_header_value(headers, "Content-Type", 0, "text/plain");
  EXPECT_STREQ("text/plain", val);
}

TEST(GetHeaderValueTest, DefaultValueInt) {
  Headers headers = {{"Dummy", "Dummy"}};
  auto val =
      detail::get_header_value<uint64_t>(headers, "Content-Length", 0, 100);
  EXPECT_EQ(100ull, val);
}

TEST(GetHeaderValueTest, RegularValue) {
  Headers headers = {{"Content-Type", "text/html"}, {"Dummy", "Dummy"}};
  auto val = detail::get_header_value(headers, "Content-Type", 0, "text/plain");
  EXPECT_STREQ("text/html", val);
}

TEST(GetHeaderValueTest, RegularValueWithDifferentCase) {
  Headers headers = {{"Content-Type", "text/html"}, {"Dummy", "Dummy"}};
  auto val = detail::get_header_value(headers, "content-type", 0, "text/plain");
  EXPECT_STREQ("text/html", val);
}

TEST(GetHeaderValueTest, SetContent) {
  Response res;

  res.set_content("html", "text/html");
  EXPECT_EQ("text/html", res.get_header_value("Content-Type"));

  res.set_content("text", "text/plain");
  EXPECT_EQ(1U, res.get_header_value_count("Content-Type"));
  EXPECT_EQ("text/plain", res.get_header_value("Content-Type"));
}

TEST(GetHeaderValueTest, RegularValueInt) {
  Headers headers = {{"Content-Length", "100"}, {"Dummy", "Dummy"}};
  auto val =
      detail::get_header_value<uint64_t>(headers, "Content-Length", 0, 0);
  EXPECT_EQ(100ull, val);
}

TEST(GetHeaderValueTest, Range) {
  {
    Headers headers = {make_range_header({{1, -1}})};
    auto val = detail::get_header_value(headers, "Range", 0, 0);
    EXPECT_STREQ("bytes=1-", val);
  }

  {
    Headers headers = {make_range_header({{-1, 1}})};
    auto val = detail::get_header_value(headers, "Range", 0, 0);
    EXPECT_STREQ("bytes=-1", val);
  }

  {
    Headers headers = {make_range_header({{1, 10}})};
    auto val = detail::get_header_value(headers, "Range", 0, 0);
    EXPECT_STREQ("bytes=1-10", val);
  }

  {
    Headers headers = {make_range_header({{1, 10}, {100, -1}})};
    auto val = detail::get_header_value(headers, "Range", 0, 0);
    EXPECT_STREQ("bytes=1-10, 100-", val);
  }

  {
    Headers headers = {make_range_header({{1, 10}, {100, 200}})};
    auto val = detail::get_header_value(headers, "Range", 0, 0);
    EXPECT_STREQ("bytes=1-10, 100-200", val);
  }

  {
    Headers headers = {make_range_header({{0, 0}, {-1, 1}})};
    auto val = detail::get_header_value(headers, "Range", 0, 0);
    EXPECT_STREQ("bytes=0-0, -1", val);
  }
}

TEST(ParseHeaderValueTest, Range) {
  {
    Ranges ranges;
    auto ret = detail::parse_range_header("bytes=1-", ranges);
    EXPECT_TRUE(ret);
    EXPECT_EQ(1u, ranges.size());
    EXPECT_EQ(1u, ranges[0].first);
    EXPECT_EQ(-1, ranges[0].second);
  }

  {
    Ranges ranges;
    auto ret = detail::parse_range_header("bytes=-1", ranges);
    EXPECT_TRUE(ret);
    EXPECT_EQ(1u, ranges.size());
    EXPECT_EQ(-1, ranges[0].first);
    EXPECT_EQ(1u, ranges[0].second);
  }

  {
    Ranges ranges;
    auto ret = detail::parse_range_header("bytes=1-10", ranges);
    EXPECT_TRUE(ret);
    EXPECT_EQ(1u, ranges.size());
    EXPECT_EQ(1u, ranges[0].first);
    EXPECT_EQ(10u, ranges[0].second);
  }

  {
    Ranges ranges;
    auto ret = detail::parse_range_header("bytes=10-1", ranges);
    EXPECT_FALSE(ret);
  }

  {
    Ranges ranges;
    auto ret = detail::parse_range_header("bytes=1-10, 100-", ranges);
    EXPECT_TRUE(ret);
    EXPECT_EQ(2u, ranges.size());
    EXPECT_EQ(1u, ranges[0].first);
    EXPECT_EQ(10u, ranges[0].second);
    EXPECT_EQ(100u, ranges[1].first);
    EXPECT_EQ(-1, ranges[1].second);
  }

  {
    Ranges ranges;
    auto ret =
        detail::parse_range_header("bytes=1-10, 100-200, 300-400", ranges);
    EXPECT_TRUE(ret);
    EXPECT_EQ(3u, ranges.size());
    EXPECT_EQ(1u, ranges[0].first);
    EXPECT_EQ(10u, ranges[0].second);
    EXPECT_EQ(100u, ranges[1].first);
    EXPECT_EQ(200u, ranges[1].second);
    EXPECT_EQ(300u, ranges[2].first);
    EXPECT_EQ(400u, ranges[2].second);
  }
}

TEST(ParseAcceptEncoding1, AcceptEncoding) {
  Request req;
  req.set_header("Accept-Encoding", "gzip");

  Response res;
  res.set_header("Content-Type", "text/plain");

  auto ret = detail::encoding_type(req, res);

#ifdef CPPHTTPLIB_ZLIB_SUPPORT
  EXPECT_TRUE(ret == detail::EncodingType::Gzip);
#else
  EXPECT_TRUE(ret == detail::EncodingType::None);
#endif
}

TEST(ParseAcceptEncoding2, AcceptEncoding) {
  Request req;
  req.set_header("Accept-Encoding", "gzip, deflate, br");

  Response res;
  res.set_header("Content-Type", "text/plain");

  auto ret = detail::encoding_type(req, res);

#ifdef CPPHTTPLIB_BROTLI_SUPPORT
  EXPECT_TRUE(ret == detail::EncodingType::Brotli);
#elif CPPHTTPLIB_ZLIB_SUPPORT
  EXPECT_TRUE(ret == detail::EncodingType::Gzip);
#else
  EXPECT_TRUE(ret == detail::EncodingType::None);
#endif
}

TEST(ParseAcceptEncoding3, AcceptEncoding) {
  Request req;
  req.set_header("Accept-Encoding", "br;q=1.0, gzip;q=0.8, *;q=0.1");

  Response res;
  res.set_header("Content-Type", "text/plain");

  auto ret = detail::encoding_type(req, res);

#ifdef CPPHTTPLIB_BROTLI_SUPPORT
  EXPECT_TRUE(ret == detail::EncodingType::Brotli);
#elif CPPHTTPLIB_ZLIB_SUPPORT
  EXPECT_TRUE(ret == detail::EncodingType::Gzip);
#else
  EXPECT_TRUE(ret == detail::EncodingType::None);
#endif
}

TEST(BufferStreamTest, read) {
  detail::BufferStream strm1;
  Stream &strm = strm1;

  EXPECT_EQ(5, strm.write("hello"));

  char buf[512];
  EXPECT_EQ(2, strm.read(buf, 2));
  EXPECT_EQ('h', buf[0]);
  EXPECT_EQ('e', buf[1]);

  EXPECT_EQ(2, strm.read(buf, 2));
  EXPECT_EQ('l', buf[0]);
  EXPECT_EQ('l', buf[1]);

  EXPECT_EQ(1, strm.read(buf, 1));
  EXPECT_EQ('o', buf[0]);

  EXPECT_EQ(0, strm.read(buf, 1));
}

TEST(ChunkedEncodingTest, FromHTTPWatch_Online) {
  auto host = "www.httpwatch.com";

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  auto port = 443;
  SSLClient cli(host, port);
#else
  auto port = 80;
  Client cli(host, port);
#endif
  cli.set_connection_timeout(2);

  auto res =
      cli.Get("/httpgallery/chunked/chunkedimage.aspx?0.4153841143030137");
  ASSERT_TRUE(res);

  std::string out;
  detail::read_file("./image.jpg", out);

  EXPECT_EQ(200, res->status);
  EXPECT_EQ(out, res->body);
}

TEST(HostnameToIPConversionTest, HTTPWatch_Online) {
  auto host = "www.httpwatch.com";

  auto ip = hosted_at(host);
  EXPECT_EQ("191.236.16.12", ip);

  std::vector<std::string> addrs;
  hosted_at(host, addrs);
  EXPECT_EQ(1u, addrs.size());
}

#if 0 // It depends on each test environment...
TEST(HostnameToIPConversionTest, YouTube_Online) {
  auto host = "www.youtube.com";

  std::vector<std::string> addrs;
  hosted_at(host, addrs);

  EXPECT_EQ(20u, addrs.size());

  auto it = std::find(addrs.begin(), addrs.end(), "2607:f8b0:4006:809::200e");
  EXPECT_TRUE(it != addrs.end());
}
#endif

TEST(ChunkedEncodingTest, WithContentReceiver_Online) {
  auto host = "www.httpwatch.com";

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  auto port = 443;
  SSLClient cli(host, port);
#else
  auto port = 80;
  Client cli(host, port);
#endif
  cli.set_connection_timeout(2);

  std::string body;
  auto res =
      cli.Get("/httpgallery/chunked/chunkedimage.aspx?0.4153841143030137",
              [&](const char *data, size_t data_length) {
                body.append(data, data_length);
                return true;
              });
  ASSERT_TRUE(res);

  std::string out;
  detail::read_file("./image.jpg", out);

  EXPECT_EQ(200, res->status);
  EXPECT_EQ(out, body);
}

TEST(ChunkedEncodingTest, WithResponseHandlerAndContentReceiver_Online) {
  auto host = "www.httpwatch.com";

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  auto port = 443;
  SSLClient cli(host, port);
#else
  auto port = 80;
  Client cli(host, port);
#endif
  cli.set_connection_timeout(2);

  std::string body;
  auto res = cli.Get(
      "/httpgallery/chunked/chunkedimage.aspx?0.4153841143030137",
      [&](const Response &response) {
        EXPECT_EQ(200, response.status);
        return true;
      },
      [&](const char *data, size_t data_length) {
        body.append(data, data_length);
        return true;
      });
  ASSERT_TRUE(res);

  std::string out;
  detail::read_file("./image.jpg", out);

  EXPECT_EQ(200, res->status);
  EXPECT_EQ(out, body);
}

TEST(RangeTest, FromHTTPBin_Online) {
  auto host = "httpbin.org";

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  auto port = 443;
  SSLClient cli(host, port);
#else
  auto port = 80;
  Client cli(host, port);
#endif
  cli.set_connection_timeout(5);

  {
    auto res = cli.Get("/range/32");
    ASSERT_TRUE(res);
    EXPECT_EQ("abcdefghijklmnopqrstuvwxyzabcdef", res->body);
    EXPECT_EQ(200, res->status);
  }

  {
    Headers headers = {make_range_header({{1, -1}})};
    auto res = cli.Get("/range/32", headers);
    ASSERT_TRUE(res);
    EXPECT_EQ("bcdefghijklmnopqrstuvwxyzabcdef", res->body);
    EXPECT_EQ(206, res->status);
  }

  {
    Headers headers = {make_range_header({{1, 10}})};
    auto res = cli.Get("/range/32", headers);
    ASSERT_TRUE(res);
    EXPECT_EQ("bcdefghijk", res->body);
    EXPECT_EQ(206, res->status);
  }

  {
    Headers headers = {make_range_header({{0, 31}})};
    auto res = cli.Get("/range/32", headers);
    ASSERT_TRUE(res);
    EXPECT_EQ("abcdefghijklmnopqrstuvwxyzabcdef", res->body);
    EXPECT_EQ(200, res->status);
  }

  {
    Headers headers = {make_range_header({{0, -1}})};
    auto res = cli.Get("/range/32", headers);
    ASSERT_TRUE(res);
    EXPECT_EQ("abcdefghijklmnopqrstuvwxyzabcdef", res->body);
    EXPECT_EQ(200, res->status);
  }

  {
    Headers headers = {make_range_header({{0, 32}})};
    auto res = cli.Get("/range/32", headers);
    ASSERT_TRUE(res);
    EXPECT_EQ(416, res->status);
  }
}

TEST(ConnectionErrorTest, InvalidHost) {
  auto host = "-abcde.com";

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  auto port = 443;
  SSLClient cli(host, port);
#else
  auto port = 80;
  Client cli(host, port);
#endif
  cli.set_connection_timeout(std::chrono::seconds(2));

  auto res = cli.Get("/");
  ASSERT_TRUE(!res);
  EXPECT_EQ(Error::Connection, res.error());
}

TEST(ConnectionErrorTest, InvalidHost2) {
  auto host = "httpbin.org/";

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  SSLClient cli(host);
#else
  Client cli(host);
#endif
  cli.set_connection_timeout(std::chrono::seconds(2));

  auto res = cli.Get("/");
  ASSERT_TRUE(!res);
  EXPECT_EQ(Error::Connection, res.error());
}

TEST(ConnectionErrorTest, InvalidHostCheckResultErrorToString) {
  auto host = "httpbin.org/";

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  SSLClient cli(host);
#else
  Client cli(host);
#endif
  cli.set_connection_timeout(std::chrono::seconds(2));

  auto res = cli.Get("/");
  ASSERT_TRUE(!res);
  stringstream s;
  s << "error code: " << res.error();
  EXPECT_EQ("error code: Could not establish connection (2)", s.str());
}

TEST(ConnectionErrorTest, InvalidPort) {
  auto host = "localhost";
  auto port = 44380;

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  SSLClient cli(host, port);
#else
  Client cli(host, port);
#endif
  cli.set_connection_timeout(std::chrono::seconds(2));

  auto res = cli.Get("/");
  ASSERT_TRUE(!res);
  EXPECT_TRUE(Error::Connection == res.error() ||
              Error::ConnectionTimeout == res.error());
}

TEST(ConnectionErrorTest, Timeout_Online) {
  auto host = "google.com";

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  auto port = 44380;
  SSLClient cli(host, port);
#else
  auto port = 8080;
  Client cli(host, port);
#endif
  cli.set_connection_timeout(std::chrono::seconds(2));

  // only probe one address type so that the error reason
  // correlates to the timed-out IPv4, not the unsupported
  // IPv6 connection attempt
  cli.set_address_family(AF_INET);

  auto res = cli.Get("/");
  ASSERT_TRUE(!res);
  EXPECT_EQ(Error::ConnectionTimeout, res.error());
}

TEST(CancelTest, NoCancel_Online) {
  auto host = "httpbin.org";

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  auto port = 443;
  SSLClient cli(host, port);
#else
  auto port = 80;
  Client cli(host, port);
#endif
  cli.set_connection_timeout(std::chrono::seconds(5));

  auto res = cli.Get("/range/32", [](uint64_t, uint64_t) { return true; });
  ASSERT_TRUE(res);
  EXPECT_EQ("abcdefghijklmnopqrstuvwxyzabcdef", res->body);
  EXPECT_EQ(200, res->status);
}

TEST(CancelTest, WithCancelSmallPayload_Online) {
  auto host = "httpbin.org";

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  auto port = 443;
  SSLClient cli(host, port);
#else
  auto port = 80;
  Client cli(host, port);
#endif

  auto res = cli.Get("/range/32", [](uint64_t, uint64_t) { return false; });
  cli.set_connection_timeout(std::chrono::seconds(5));
  ASSERT_TRUE(!res);
  EXPECT_EQ(Error::Canceled, res.error());
}

TEST(CancelTest, WithCancelLargePayload_Online) {
  auto host = "httpbin.org";

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  auto port = 443;
  SSLClient cli(host, port);
#else
  auto port = 80;
  Client cli(host, port);
#endif
  cli.set_connection_timeout(std::chrono::seconds(5));

  uint32_t count = 0;
  auto res = cli.Get("/range/65536",
                     [&count](uint64_t, uint64_t) { return (count++ == 0); });
  ASSERT_TRUE(!res);
  EXPECT_EQ(Error::Canceled, res.error());
}

TEST(BaseAuthTest, FromHTTPWatch_Online) {
  auto host = "httpbin.org";

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  auto port = 443;
  SSLClient cli(host, port);
#else
  auto port = 80;
  Client cli(host, port);
#endif

  {
    auto res = cli.Get("/basic-auth/hello/world");
    ASSERT_TRUE(res);
    EXPECT_EQ(401, res->status);
  }

  {
    auto res = cli.Get("/basic-auth/hello/world",
                       {make_basic_authentication_header("hello", "world")});
    ASSERT_TRUE(res);
    EXPECT_EQ("{\n  \"authenticated\": true, \n  \"user\": \"hello\"\n}\n",
              res->body);
    EXPECT_EQ(200, res->status);
  }

  {
    cli.set_basic_auth("hello", "world");
    auto res = cli.Get("/basic-auth/hello/world");
    ASSERT_TRUE(res);
    EXPECT_EQ("{\n  \"authenticated\": true, \n  \"user\": \"hello\"\n}\n",
              res->body);
    EXPECT_EQ(200, res->status);
  }

  {
    cli.set_basic_auth("hello", "bad");
    auto res = cli.Get("/basic-auth/hello/world");
    ASSERT_TRUE(res);
    EXPECT_EQ(401, res->status);
  }

  {
    cli.set_basic_auth("bad", "world");
    auto res = cli.Get("/basic-auth/hello/world");
    ASSERT_TRUE(res);
    EXPECT_EQ(401, res->status);
  }
}

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
TEST(DigestAuthTest, FromHTTPWatch_Online) {
  auto host = "httpbin.org";
  auto port = 443;
  SSLClient cli(host, port);

  {
    auto res = cli.Get("/digest-auth/auth/hello/world");
    ASSERT_TRUE(res);
    EXPECT_EQ(401, res->status);
  }

  {
    std::vector<std::string> paths = {
        "/digest-auth/auth/hello/world/MD5",
        "/digest-auth/auth/hello/world/SHA-256",
        "/digest-auth/auth/hello/world/SHA-512",
        "/digest-auth/auth-int/hello/world/MD5",
    };

    cli.set_digest_auth("hello", "world");
    for (auto path : paths) {
      auto res = cli.Get(path.c_str());
      ASSERT_TRUE(res);
      EXPECT_EQ("{\n  \"authenticated\": true, \n  \"user\": \"hello\"\n}\n",
                res->body);
      EXPECT_EQ(200, res->status);
    }

    cli.set_digest_auth("hello", "bad");
    for (auto path : paths) {
      auto res = cli.Get(path.c_str());
      ASSERT_TRUE(res);
      EXPECT_EQ(401, res->status);
    }

    // NOTE: Until httpbin.org fixes issue #46, the following test is commented
    // out. Please see https://httpbin.org/digest-auth/auth/hello/world
    // cli.set_digest_auth("bad", "world");
    // for (auto path : paths) {
    //   auto res = cli.Get(path.c_str());
    //   ASSERT_TRUE(res);
    //   EXPECT_EQ(400, res->status);
    // }
  }
}
#endif

TEST(SpecifyServerIPAddressTest, AnotherHostname_Online) {
  auto host = "google.com";
  auto another_host = "example.com";
  auto wrong_ip = "0.0.0.0";

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  SSLClient cli(host);
#else
  Client cli(host);
#endif

  cli.set_hostname_addr_map({{another_host, wrong_ip}});
  auto res = cli.Get("/");
  ASSERT_TRUE(res);
  ASSERT_EQ(301, res->status);
}

TEST(SpecifyServerIPAddressTest, RealHostname_Online) {
  auto host = "google.com";
  auto wrong_ip = "0.0.0.0";

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  SSLClient cli(host);
#else
  Client cli(host);
#endif

  cli.set_hostname_addr_map({{host, wrong_ip}});
  auto res = cli.Get("/");
  ASSERT_TRUE(!res);
  EXPECT_EQ(Error::Connection, res.error());
}

TEST(AbsoluteRedirectTest, Redirect_Online) {
  auto host = "nghttp2.org";

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  SSLClient cli(host);
#else
  Client cli(host);
#endif

  cli.set_follow_location(true);
  auto res = cli.Get("/httpbin/absolute-redirect/3");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

TEST(RedirectTest, Redirect_Online) {
  auto host = "nghttp2.org";

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  SSLClient cli(host);
#else
  Client cli(host);
#endif

  cli.set_follow_location(true);
  auto res = cli.Get("/httpbin/redirect/3");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

TEST(RelativeRedirectTest, Redirect_Online) {
  auto host = "nghttp2.org";

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  SSLClient cli(host);
#else
  Client cli(host);
#endif

  cli.set_follow_location(true);
  auto res = cli.Get("/httpbin/relative-redirect/3");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

TEST(TooManyRedirectTest, Redirect_Online) {
  auto host = "nghttp2.org";

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  SSLClient cli(host);
#else
  Client cli(host);
#endif

  cli.set_follow_location(true);
  auto res = cli.Get("/httpbin/redirect/21");
  ASSERT_TRUE(!res);
  EXPECT_EQ(Error::ExceedRedirectCount, res.error());
}

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
TEST(YahooRedirectTest, Redirect_Online) {
  Client cli("yahoo.com");

  auto res = cli.Get("/");
  ASSERT_TRUE(res);
  EXPECT_EQ(301, res->status);

  cli.set_follow_location(true);
  res = cli.Get("/");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("https://yahoo.com/", res->location);
}

TEST(HttpsToHttpRedirectTest, Redirect_Online) {
  SSLClient cli("nghttp2.org");
  cli.set_follow_location(true);
  auto res = cli.Get(
      "/httpbin/redirect-to?url=http%3A%2F%2Fwww.google.com&status_code=302");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

TEST(HttpsToHttpRedirectTest2, Redirect_Online) {
  SSLClient cli("nghttp2.org");
  cli.set_follow_location(true);

  Params params;
  params.emplace("url", "http://www.google.com");
  params.emplace("status_code", "302");

  auto res = cli.Get("/httpbin/redirect-to", params, Headers{});
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

TEST(HttpsToHttpRedirectTest3, Redirect_Online) {
  SSLClient cli("nghttp2.org");
  cli.set_follow_location(true);

  Params params;
  params.emplace("url", "http://www.google.com");

  auto res = cli.Get("/httpbin/redirect-to?status_code=302", params, Headers{});
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

TEST(UrlWithSpace, Redirect_Online) {
  SSLClient cli("edge.forgecdn.net");
  cli.set_follow_location(true);

  auto res = cli.Get("/files/2595/310/Neat 1.4-17.jar");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ(18527U, res->get_header_value<uint64_t>("Content-Length"));
}

#endif

#if !defined(_WIN32) && !defined(_WIN64)
TEST(ReceiveSignals, Signal) {
  auto setupSignalHandlers = []() {
    struct sigaction act;

    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = [](int sig, siginfo_t *, void *) {
      switch (sig) {
      case SIGINT:
      default: break;
      }
    };
    ::sigaction(SIGINT, &act, nullptr);
  };

  Server svr;
  int port = 0;
  auto thread = std::thread([&]() {
    setupSignalHandlers();
    port = svr.bind_to_any_port("localhost");
    svr.listen_after_bind();
  });
  auto se = detail::scope_exit([&] {
    svr.stop();
    thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();

  ASSERT_TRUE(svr.is_running());
  pthread_kill(thread.native_handle(), SIGINT);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ASSERT_TRUE(svr.is_running());
}
#endif

TEST(RedirectToDifferentPort, Redirect) {
  Server svr1;
  svr1.Get("/1", [&](const Request & /*req*/, Response &res) {
    res.set_content("Hello World!", "text/plain");
  });

  int svr1_port = 0;
  auto thread1 = std::thread([&]() {
    svr1_port = svr1.bind_to_any_port("localhost");
    svr1.listen_after_bind();
  });

  Server svr2;
  svr2.Get("/2", [&](const Request & /*req*/, Response &res) {
    res.set_redirect("http://localhost:" + std::to_string(svr1_port) + "/1");
  });

  int svr2_port = 0;
  auto thread2 = std::thread([&]() {
    svr2_port = svr2.bind_to_any_port("localhost");
    svr2.listen_after_bind();
  });
  auto se = detail::scope_exit([&] {
    svr2.stop();
    thread2.join();
    svr1.stop();
    thread1.join();
    ASSERT_FALSE(svr2.is_running());
    ASSERT_FALSE(svr1.is_running());
  });

  while (!svr1.is_running() || !svr2.is_running()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Give GET time to get a few messages.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  Client cli("localhost", svr2_port);
  cli.set_follow_location(true);

  auto res = cli.Get("/2");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("Hello World!", res->body);
}

TEST(RedirectFromPageWithContent, Redirect) {
  Server svr;

  svr.Get("/1", [&](const Request & /*req*/, Response &res) {
    res.set_content("___", "text/plain");
    res.set_redirect("/2");
  });

  svr.Get("/2", [&](const Request & /*req*/, Response &res) {
    res.set_content("Hello World!", "text/plain");
  });

  auto th = std::thread([&]() { svr.listen("localhost", PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    th.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();

  // Give GET time to get a few messages.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  {
    Client cli("localhost", PORT);
    cli.set_follow_location(true);

    std::string body;
    auto res = cli.Get("/1", [&](const char *data, size_t data_length) {
      body.append(data, data_length);
      return true;
    });

    ASSERT_TRUE(res);
    EXPECT_EQ(200, res->status);
    EXPECT_EQ("Hello World!", body);
  }

  {
    Client cli("localhost", PORT);

    std::string body;
    auto res = cli.Get("/1", [&](const char *data, size_t data_length) {
      body.append(data, data_length);
      return true;
    });

    ASSERT_TRUE(res);
    EXPECT_EQ(302, res->status);
    EXPECT_EQ("___", body);
  }
}

TEST(RedirectFromPageWithContentIP6, Redirect) {
  Server svr;

  svr.Get("/1", [&](const Request & /*req*/, Response &res) {
    res.set_content("___", "text/plain");
    // res.set_redirect("/2");
    res.set_redirect("http://[::1]:1234/2");
  });

  svr.Get("/2", [&](const Request &req, Response &res) {
    auto host_header = req.headers.find("Host");
    ASSERT_TRUE(host_header != req.headers.end());
    EXPECT_EQ("[::1]:1234", host_header->second);

    res.set_content("Hello World!", "text/plain");
  });

  auto th = std::thread([&]() { svr.listen("::1", 1234); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    th.join();
    ASSERT_FALSE(svr.is_running());
  });

  // When IPV6 support isn't available svr.listen("::1", 1234) never
  // actually starts anything, so the condition !svr.is_running() will
  // always remain true, and the loop never stops.
  // This basically counts how many milliseconds have passed since the
  // call to svr.listen(), and if after 5 seconds nothing started yet
  // aborts the test.
  for (unsigned int milliseconds = 0; !svr.is_running(); milliseconds++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_LT(milliseconds, 5000U);
  }

  // Give GET time to get a few messages.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  {
    Client cli("http://[::1]:1234");
    cli.set_follow_location(true);

    std::string body;
    auto res = cli.Get("/1", [&](const char *data, size_t data_length) {
      body.append(data, data_length);
      return true;
    });

    ASSERT_TRUE(res);
    EXPECT_EQ(200, res->status);
    EXPECT_EQ("Hello World!", body);
  }

  {
    Client cli("http://[::1]:1234");

    std::string body;
    auto res = cli.Get("/1", [&](const char *data, size_t data_length) {
      body.append(data, data_length);
      return true;
    });

    ASSERT_TRUE(res);
    EXPECT_EQ(302, res->status);
    EXPECT_EQ("___", body);
  }
}

TEST(PathUrlEncodeTest, PathUrlEncode) {
  Server svr;

  svr.Get("/foo", [](const Request &req, Response &res) {
    auto a = req.params.find("a");
    if (a != req.params.end()) {
      res.set_content((*a).second, "text/plain");
      res.status = 200;
    } else {
      res.status = 400;
    }
  });

  auto thread = std::thread([&]() { svr.listen(HOST, PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  // Give GET time to get a few messages.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  {
    Client cli(HOST, PORT);
    cli.set_url_encode(false);

    auto res = cli.Get("/foo?a=explicitly+encoded");
    ASSERT_TRUE(res);
    EXPECT_EQ(200, res->status);
    // This expects it back with a space, as the `+` won't have been
    // url-encoded, and server-side the params get decoded turning `+`
    // into spaces.
    EXPECT_EQ("explicitly encoded", res->body);
  }
}

TEST(BindServerTest, DISABLED_BindDualStack) {
  Server svr;

  svr.Get("/1", [&](const Request & /*req*/, Response &res) {
    res.set_content("Hello World!", "text/plain");
  });

  auto thread = std::thread([&]() { svr.listen("::", PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  // Give GET time to get a few messages.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  {
    Client cli("127.0.0.1", PORT);

    auto res = cli.Get("/1");
    ASSERT_TRUE(res);
    EXPECT_EQ(200, res->status);
    EXPECT_EQ("Hello World!", res->body);
  }
  {
    Client cli("::1", PORT);

    auto res = cli.Get("/1");
    ASSERT_TRUE(res);
    EXPECT_EQ(200, res->status);
    EXPECT_EQ("Hello World!", res->body);
  }
}

TEST(BindServerTest, BindAndListenSeparately) {
  Server svr;
  int port = svr.bind_to_any_port("0.0.0.0");
  ASSERT_TRUE(svr.is_valid());
  ASSERT_TRUE(port > 0);
  svr.stop();
}

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
TEST(BindServerTest, BindAndListenSeparatelySSL) {
  SSLServer svr(SERVER_CERT_FILE, SERVER_PRIVATE_KEY_FILE, CLIENT_CA_CERT_FILE,
                CLIENT_CA_CERT_DIR);
  int port = svr.bind_to_any_port("0.0.0.0");
  ASSERT_TRUE(svr.is_valid());
  ASSERT_TRUE(port > 0);
  svr.stop();
}
#endif

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
TEST(BindServerTest, BindAndListenSeparatelySSLEncryptedKey) {
  SSLServer svr(SERVER_ENCRYPTED_CERT_FILE, SERVER_ENCRYPTED_PRIVATE_KEY_FILE,
                nullptr, nullptr, SERVER_ENCRYPTED_PRIVATE_KEY_PASS);
  int port = svr.bind_to_any_port("0.0.0.0");
  ASSERT_TRUE(svr.is_valid());
  ASSERT_TRUE(port > 0);
  svr.stop();
}
#endif

TEST(ErrorHandlerTest, ContentLength) {
  Server svr;

  svr.set_error_handler([](const Request & /*req*/, Response &res) {
    res.status = 200;
    res.set_content("abcdefghijklmnopqrstuvwxyz",
                    "text/html"); // <= Content-Length still 13
  });

  svr.Get("/hi", [](const Request & /*req*/, Response &res) {
    res.set_content("Hello World!\n", "text/plain");
    res.status = 524;
  });

  auto thread = std::thread([&]() { svr.listen(HOST, PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  // Give GET time to get a few messages.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  {
    Client cli(HOST, PORT);

    auto res = cli.Get("/hi");
    ASSERT_TRUE(res);
    EXPECT_EQ(200, res->status);
    EXPECT_EQ("text/html", res->get_header_value("Content-Type"));
    EXPECT_EQ("26", res->get_header_value("Content-Length"));
    EXPECT_EQ("abcdefghijklmnopqrstuvwxyz", res->body);
  }
}

#ifndef CPPHTTPLIB_NO_EXCEPTIONS
TEST(ExceptionHandlerTest, ContentLength) {
  Server svr;

  svr.set_exception_handler([](const Request & /*req*/, Response &res,
                               std::exception_ptr ep) {
    EXPECT_FALSE(ep == nullptr);
    try {
      std::rethrow_exception(ep);
    } catch (std::exception &e) { EXPECT_EQ("abc", std::string(e.what())); }
    res.status = 500;
    res.set_content("abcdefghijklmnopqrstuvwxyz",
                    "text/html"); // <= Content-Length still 13 at this point
  });

  svr.Get("/hi", [](const Request & /*req*/, Response &res) {
    res.set_content("Hello World!\n", "text/plain");
    throw std::runtime_error("abc");
  });

  auto thread = std::thread([&]() { svr.listen(HOST, PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  // Give GET time to get a few messages.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  for (size_t i = 0; i < 10; i++) {
    Client cli(HOST, PORT);

    for (size_t j = 0; j < 100; j++) {
      auto res = cli.Get("/hi");
      ASSERT_TRUE(res);
      EXPECT_EQ(500, res->status);
      EXPECT_EQ("text/html", res->get_header_value("Content-Type"));
      EXPECT_EQ("26", res->get_header_value("Content-Length"));
      EXPECT_EQ("abcdefghijklmnopqrstuvwxyz", res->body);
    }

    cli.set_keep_alive(true);

    for (size_t j = 0; j < 100; j++) {
      auto res = cli.Get("/hi");
      ASSERT_TRUE(res);
      EXPECT_EQ(500, res->status);
      EXPECT_EQ("text/html", res->get_header_value("Content-Type"));
      EXPECT_EQ("26", res->get_header_value("Content-Length"));
      EXPECT_EQ("abcdefghijklmnopqrstuvwxyz", res->body);
    }
  }
}
#endif

TEST(NoContentTest, ContentLength) {
  Server svr;

  svr.Get("/hi",
          [](const Request & /*req*/, Response &res) { res.status = 204; });
  auto thread = std::thread([&]() { svr.listen(HOST, PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  // Give GET time to get a few messages.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  {
    Client cli(HOST, PORT);

    auto res = cli.Get("/hi");
    ASSERT_TRUE(res);
    EXPECT_EQ(204, res->status);
    EXPECT_EQ("0", res->get_header_value("Content-Length"));
  }
}

TEST(RoutingHandlerTest, PreRoutingHandler) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  SSLServer svr(SERVER_CERT_FILE, SERVER_PRIVATE_KEY_FILE);
  ASSERT_TRUE(svr.is_valid());
#else
  Server svr;
#endif

  svr.set_pre_routing_handler([](const Request &req, Response &res) {
    if (req.path == "/routing_handler") {
      res.set_header("PRE_ROUTING", "on");
      res.set_content("Routing Handler", "text/plain");
      return httplib::Server::HandlerResponse::Handled;
    }
    return httplib::Server::HandlerResponse::Unhandled;
  });

  svr.set_error_handler([](const Request & /*req*/, Response &res) {
    res.set_content("Error", "text/html");
  });

  svr.set_post_routing_handler([](const Request &req, Response &res) {
    if (req.path == "/routing_handler") {
      res.set_header("POST_ROUTING", "on");
    }
  });

  svr.Get("/hi", [](const Request & /*req*/, Response &res) {
    res.set_content("Hello World!\n", "text/plain");
  });

  auto thread = std::thread([&]() { svr.listen(HOST, PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  // Give GET time to get a few messages.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    SSLClient cli(HOST, PORT);
    cli.enable_server_certificate_verification(false);
#else
    Client cli(HOST, PORT);
#endif

    auto res = cli.Get("/routing_handler");
    ASSERT_TRUE(res);
    EXPECT_EQ(200, res->status);
    EXPECT_EQ("Routing Handler", res->body);
    EXPECT_EQ(1U, res->get_header_value_count("PRE_ROUTING"));
    EXPECT_EQ("on", res->get_header_value("PRE_ROUTING"));
    EXPECT_EQ(1U, res->get_header_value_count("POST_ROUTING"));
    EXPECT_EQ("on", res->get_header_value("POST_ROUTING"));
  }

  {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    SSLClient cli(HOST, PORT);
    cli.enable_server_certificate_verification(false);
#else
    Client cli(HOST, PORT);
#endif

    auto res = cli.Get("/hi");
    ASSERT_TRUE(res);
    EXPECT_EQ(200, res->status);
    EXPECT_EQ("Hello World!\n", res->body);
    EXPECT_EQ(0U, res->get_header_value_count("PRE_ROUTING"));
    EXPECT_EQ(0U, res->get_header_value_count("POST_ROUTING"));
  }

  {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    SSLClient cli(HOST, PORT);
    cli.enable_server_certificate_verification(false);
#else
    Client cli(HOST, PORT);
#endif

    auto res = cli.Get("/aaa");
    ASSERT_TRUE(res);
    EXPECT_EQ(404, res->status);
    EXPECT_EQ("Error", res->body);
    EXPECT_EQ(0U, res->get_header_value_count("PRE_ROUTING"));
    EXPECT_EQ(0U, res->get_header_value_count("POST_ROUTING"));
  }
}

TEST(InvalidFormatTest, StatusCode) {
  Server svr;

  svr.Get("/hi", [](const Request & /*req*/, Response &res) {
    res.set_content("Hello World!\n", "text/plain");
    res.status = 9999; // Status should be a three-digit code...
  });

  auto thread = std::thread([&]() { svr.listen(HOST, PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  // Give GET time to get a few messages.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  {
    Client cli(HOST, PORT);

    auto res = cli.Get("/hi");
    ASSERT_FALSE(res);
  }
}

TEST(URLFragmentTest, WithFragment) {
  Server svr;

  svr.Get("/hi", [](const Request &req, Response & /*res*/) {
    EXPECT_TRUE(req.target == "/hi");
  });

  auto thread = std::thread([&]() { svr.listen(HOST, PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  std::this_thread::sleep_for(std::chrono::seconds(1));

  {
    Client cli(HOST, PORT);

    auto res = cli.Get("/hi#key1=val1=key2=val2");
    EXPECT_TRUE(res);
    EXPECT_EQ(200, res->status);

    res = cli.Get("/hi%23key1=val1=key2=val2");
    EXPECT_TRUE(res);
    EXPECT_EQ(404, res->status);
  }
}

class ServerTest : public ::testing::Test {
protected:
  ServerTest()
      : cli_(HOST, PORT)
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        ,
        svr_(SERVER_CERT_FILE, SERVER_PRIVATE_KEY_FILE)
#endif
  {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    cli_.enable_server_certificate_verification(false);
#endif
  }

  virtual void SetUp() {
    svr_.set_mount_point("/", "./www");
    svr_.set_mount_point("/mount", "./www2");
    svr_.set_file_extension_and_mimetype_mapping("abcde", "text/abcde");

    svr_.Get("/hi",
             [&](const Request & /*req*/, Response &res) {
               res.set_content("Hello World!", "text/plain");
             })
        .Get("/http_response_splitting",
             [&](const Request & /*req*/, Response &res) {
               res.set_header("a", "1\r\nSet-Cookie: a=1");
               EXPECT_EQ(0U, res.headers.size());
               EXPECT_FALSE(res.has_header("a"));

               res.set_header("a", "1\nSet-Cookie: a=1");
               EXPECT_EQ(0U, res.headers.size());
               EXPECT_FALSE(res.has_header("a"));

               res.set_header("a", "1\rSet-Cookie: a=1");
               EXPECT_EQ(0U, res.headers.size());
               EXPECT_FALSE(res.has_header("a"));

               res.set_header("a\r\nb", "0");
               EXPECT_EQ(0U, res.headers.size());
               EXPECT_FALSE(res.has_header("a"));

               res.set_header("a\rb", "0");
               EXPECT_EQ(0U, res.headers.size());
               EXPECT_FALSE(res.has_header("a"));

               res.set_header("a\nb", "0");
               EXPECT_EQ(0U, res.headers.size());
               EXPECT_FALSE(res.has_header("a"));

               res.set_redirect("1\r\nSet-Cookie: a=1");
               EXPECT_EQ(0U, res.headers.size());
               EXPECT_FALSE(res.has_header("Location"));
             })
        .Get("/slow",
             [&](const Request & /*req*/, Response &res) {
               std::this_thread::sleep_for(std::chrono::seconds(2));
               res.set_content("slow", "text/plain");
             })
#if 0
        .Post("/slowpost",
              [&](const Request & /*req*/, Response &res) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                res.set_content("slow", "text/plain");
              })
#endif
        .Get("/remote_addr",
             [&](const Request &req, Response &res) {
               auto remote_addr = req.headers.find("REMOTE_ADDR")->second;
               EXPECT_TRUE(req.has_header("REMOTE_PORT"));
               EXPECT_EQ(req.remote_addr, req.get_header_value("REMOTE_ADDR"));
               EXPECT_EQ(req.remote_port,
                         std::stoi(req.get_header_value("REMOTE_PORT")));
               res.set_content(remote_addr.c_str(), "text/plain");
             })
        .Get("/local_addr",
             [&](const Request &req, Response &res) {
               EXPECT_TRUE(req.has_header("LOCAL_PORT"));
               EXPECT_TRUE(req.has_header("LOCAL_ADDR"));
               auto local_addr = req.get_header_value("LOCAL_ADDR");
               auto local_port = req.get_header_value("LOCAL_PORT");
               EXPECT_EQ(req.local_addr, local_addr);
               EXPECT_EQ(req.local_port, std::stoi(local_port));
               res.set_content(local_addr.append(":").append(local_port),
                               "text/plain");
             })
        .Get("/endwith%",
             [&](const Request & /*req*/, Response &res) {
               res.set_content("Hello World!", "text/plain");
             })
        .Get("/a\\+\\+b",
             [&](const Request &req, Response &res) {
               ASSERT_TRUE(req.has_param("a +b"));
               auto val = req.get_param_value("a +b");
               res.set_content(val, "text/plain");
             })
        .Get("/", [&](const Request & /*req*/,
                      Response &res) { res.set_redirect("/hi"); })
        .Post("/1", [](const Request & /*req*/,
                       Response &res) { res.set_redirect("/2", 303); })
        .Get("/2",
             [](const Request & /*req*/, Response &res) {
               res.set_content("redirected.", "text/plain");
               res.status = 200;
             })
        .Post("/person",
              [&](const Request &req, Response &res) {
                if (req.has_param("name") && req.has_param("note")) {
                  persons_[req.get_param_value("name")] =
                      req.get_param_value("note");
                } else {
                  res.status = 400;
                }
              })
        .Put("/person",
             [&](const Request &req, Response &res) {
               if (req.has_param("name") && req.has_param("note")) {
                 persons_[req.get_param_value("name")] =
                     req.get_param_value("note");
               } else {
                 res.status = 400;
               }
             })
        .Get("/person/(.*)",
             [&](const Request &req, Response &res) {
               string name = req.matches[1];
               if (persons_.find(name) != persons_.end()) {
                 auto note = persons_[name];
                 res.set_content(note, "text/plain");
               } else {
                 res.status = 404;
               }
             })
        .Post("/x-www-form-urlencoded-json",
              [&](const Request &req, Response &res) {
                auto json = req.get_param_value("json");
                ASSERT_EQ(JSON_DATA, json);
                res.set_content(json, "appliation/json");
                res.status = 200;
              })
        .Get("/streamed-chunked",
             [&](const Request & /*req*/, Response &res) {
               res.set_chunked_content_provider(
                   "text/plain", [](size_t /*offset*/, DataSink &sink) {
                     sink.os << "123";
                     sink.os << "456";
                     sink.os << "789";
                     sink.done();
                     return true;
                   });
             })
        .Get("/streamed-chunked2",
             [&](const Request & /*req*/, Response &res) {
               auto i = new int(0);
               res.set_chunked_content_provider(
                   "text/plain",
                   [i](size_t /*offset*/, DataSink &sink) {
                     switch (*i) {
                     case 0: sink.os << "123"; break;
                     case 1: sink.os << "456"; break;
                     case 2: sink.os << "789"; break;
                     case 3: sink.done(); break;
                     }
                     (*i)++;
                     return true;
                   },
                   [i](bool success) {
                     EXPECT_TRUE(success);
                     delete i;
                   });
             })
        .Get("/streamed-chunked-with-trailer",
             [&](const Request & /*req*/, Response &res) {
               auto i = new int(0);
               res.set_header("Trailer", "Dummy1, Dummy2");
               res.set_chunked_content_provider(
                   "text/plain",
                   [i](size_t /*offset*/, DataSink &sink) {
                     switch (*i) {
                     case 0: sink.os << "123"; break;
                     case 1: sink.os << "456"; break;
                     case 2: sink.os << "789"; break;
                     case 3: {
                       sink.done_with_trailer(
                           {{"Dummy1", "DummyVal1"}, {"Dummy2", "DummyVal2"}});
                     } break;
                     }
                     (*i)++;
                     return true;
                   },
                   [i](bool success) {
                     EXPECT_TRUE(success);
                     delete i;
                   });
             })
        .Get("/streamed",
             [&](const Request & /*req*/, Response &res) {
               res.set_content_provider(
                   6, "text/plain",
                   [](size_t offset, size_t /*length*/, DataSink &sink) {
                     sink.os << (offset < 3 ? "a" : "b");
                     return true;
                   });
             })
        .Get("/streamed-with-range",
             [&](const Request & /*req*/, Response &res) {
               auto data = new std::string("abcdefg");
               res.set_content_provider(
                   data->size(), "text/plain",
                   [data](size_t offset, size_t length, DataSink &sink) {
                     size_t DATA_CHUNK_SIZE = 4;
                     const auto &d = *data;
                     auto out_len =
                         std::min(static_cast<size_t>(length), DATA_CHUNK_SIZE);
                     auto ret =
                         sink.write(&d[static_cast<size_t>(offset)], out_len);
                     EXPECT_TRUE(ret);
                     return true;
                   },
                   [data](bool success) {
                     EXPECT_TRUE(success);
                     delete data;
                   });
             })
        .Get("/streamed-cancel",
             [&](const Request & /*req*/, Response &res) {
               res.set_content_provider(
                   size_t(-1), "text/plain",
                   [](size_t /*offset*/, size_t /*length*/, DataSink &sink) {
                     sink.os << "data_chunk";
                     return true;
                   });
             })
        .Get("/with-range",
             [&](const Request & /*req*/, Response &res) {
               res.set_content("abcdefg", "text/plain");
             })
        .Post("/chunked",
              [&](const Request &req, Response & /*res*/) {
                EXPECT_EQ(req.body, "dechunked post body");
              })
        .Post("/large-chunked",
              [&](const Request &req, Response & /*res*/) {
                std::string expected(6 * 30 * 1024u, 'a');
                EXPECT_EQ(req.body, expected);
              })
        .Post("/multipart",
              [&](const Request &req, Response & /*res*/) {
                EXPECT_EQ(6u, req.files.size());
                ASSERT_TRUE(!req.has_file("???"));
                ASSERT_TRUE(req.body.empty());

                {
                  const auto &file = req.get_file_value("text1");
                  EXPECT_TRUE(file.filename.empty());
                  EXPECT_EQ("text default", file.content);
                }

                {
                  const auto &file = req.get_file_value("text2");
                  EXPECT_TRUE(file.filename.empty());
                  EXPECT_EQ("aωb", file.content);
                }

                {
                  const auto &file = req.get_file_value("file1");
                  EXPECT_EQ("hello.txt", file.filename);
                  EXPECT_EQ("text/plain", file.content_type);
                  EXPECT_EQ("h\ne\n\nl\nl\no\n", file.content);
                }

                {
                  const auto &file = req.get_file_value("file3");
                  EXPECT_TRUE(file.filename.empty());
                  EXPECT_EQ("application/octet-stream", file.content_type);
                  EXPECT_EQ(0u, file.content.size());
                }

                {
                  const auto &file = req.get_file_value("file4");
                  EXPECT_TRUE(file.filename.empty());
                  EXPECT_EQ(0u, file.content.size());
                  EXPECT_EQ("application/json  tmp-string", file.content_type);
                }
              })
        .Post("/multipart/multi_file_values",
              [&](const Request &req, Response & /*res*/) {
                EXPECT_EQ(5u, req.files.size());
                ASSERT_TRUE(!req.has_file("???"));
                ASSERT_TRUE(req.body.empty());

                {
                  const auto &text_value = req.get_file_values("text");
                  EXPECT_EQ(text_value.size(), 1);
                  auto &text = text_value[0];
                  EXPECT_TRUE(text.filename.empty());
                  EXPECT_EQ("default text", text.content);
                }
                {
                  const auto &text1_values = req.get_file_values("multi_text1");
                  EXPECT_EQ(text1_values.size(), 2);
                  EXPECT_EQ("aaaaa", text1_values[0].content);
                  EXPECT_EQ("bbbbb", text1_values[1].content);
                }

                {
                  const auto &file1_values = req.get_file_values("multi_file1");
                  EXPECT_EQ(file1_values.size(), 2);
                  auto file1 = file1_values[0];
                  EXPECT_EQ(file1.filename, "hello.txt");
                  EXPECT_EQ(file1.content_type, "text/plain");
                  EXPECT_EQ("h\ne\n\nl\nl\no\n", file1.content);

                  auto file2 = file1_values[1];
                  EXPECT_EQ(file2.filename, "world.json");
                  EXPECT_EQ(file2.content_type, "application/json");
                  EXPECT_EQ("{\n  \"world\", true\n}\n", file2.content);
                }
              })
        .Post("/empty",
              [&](const Request &req, Response &res) {
                EXPECT_EQ(req.body, "");
                EXPECT_EQ("text/plain", req.get_header_value("Content-Type"));
                EXPECT_EQ("0", req.get_header_value("Content-Length"));
                res.set_content("empty", "text/plain");
              })
        .Post("/empty-no-content-type",
              [&](const Request &req, Response &res) {
                EXPECT_EQ(req.body, "");
                EXPECT_FALSE(req.has_header("Content-Type"));
                EXPECT_EQ("0", req.get_header_value("Content-Length"));
                res.set_content("empty-no-content-type", "text/plain");
              })
        .Post("/path-only",
              [&](const Request &req, Response &res) {
                EXPECT_EQ(req.body, "");
                EXPECT_EQ("", req.get_header_value("Content-Type"));
                EXPECT_EQ("0", req.get_header_value("Content-Length"));
                res.set_content("path-only", "text/plain");
              })
        .Post("/path-headers-only",
              [&](const Request &req, Response &res) {
                EXPECT_EQ(req.body, "");
                EXPECT_EQ("", req.get_header_value("Content-Type"));
                EXPECT_EQ("0", req.get_header_value("Content-Length"));
                EXPECT_EQ("world", req.get_header_value("hello"));
                EXPECT_EQ("world2", req.get_header_value("hello2"));
                res.set_content("path-headers-only", "text/plain");
              })
        .Post("/post-large",
              [&](const Request &req, Response &res) {
                EXPECT_EQ(req.body, LARGE_DATA);
                res.set_content(req.body, "text/plain");
              })
        .Put("/empty-no-content-type",
             [&](const Request &req, Response &res) {
               EXPECT_EQ(req.body, "");
               EXPECT_FALSE(req.has_header("Content-Type"));
               EXPECT_EQ("0", req.get_header_value("Content-Length"));
               res.set_content("empty-no-content-type", "text/plain");
             })
        .Put("/put",
             [&](const Request &req, Response &res) {
               EXPECT_EQ(req.body, "PUT");
               res.set_content(req.body, "text/plain");
             })
        .Put("/put-large",
             [&](const Request &req, Response &res) {
               EXPECT_EQ(req.body, LARGE_DATA);
               res.set_content(req.body, "text/plain");
             })
        .Patch("/patch",
               [&](const Request &req, Response &res) {
                 EXPECT_EQ(req.body, "PATCH");
                 res.set_content(req.body, "text/plain");
               })
        .Delete("/delete",
                [&](const Request & /*req*/, Response &res) {
                  res.set_content("DELETE", "text/plain");
                })
        .Delete("/delete-body",
                [&](const Request &req, Response &res) {
                  EXPECT_EQ(req.body, "content");
                  res.set_content(req.body, "text/plain");
                })
        .Options(R"(\*)",
                 [&](const Request & /*req*/, Response &res) {
                   res.set_header("Allow", "GET, POST, HEAD, OPTIONS");
                 })
        .Get("/request-target",
             [&](const Request &req, Response & /*res*/) {
               EXPECT_EQ("/request-target?aaa=bbb&ccc=ddd", req.target);
               EXPECT_EQ("bbb", req.get_param_value("aaa"));
               EXPECT_EQ("ddd", req.get_param_value("ccc"));
             })
        .Get("/long-query-value",
             [&](const Request &req, Response & /*res*/) {
               EXPECT_EQ(LONG_QUERY_URL, req.target);
               EXPECT_EQ(LONG_QUERY_VALUE, req.get_param_value("key"));
             })
        .Get("/array-param",
             [&](const Request &req, Response & /*res*/) {
               EXPECT_EQ(3u, req.get_param_value_count("array"));
               EXPECT_EQ("value1", req.get_param_value("array", 0));
               EXPECT_EQ("value2", req.get_param_value("array", 1));
               EXPECT_EQ("value3", req.get_param_value("array", 2));
             })
        .Post("/validate-no-multiple-headers",
              [&](const Request &req, Response & /*res*/) {
                EXPECT_EQ(1u, req.get_header_value_count("Content-Length"));
                EXPECT_EQ("5", req.get_header_value("Content-Length"));
              })
        .Post("/content_receiver",
              [&](const Request &req, Response &res,
                  const ContentReader &content_reader) {
                if (req.is_multipart_form_data()) {
                  MultipartFormDataItems files;
                  content_reader(
                      [&](const MultipartFormData &file) {
                        files.push_back(file);
                        return true;
                      },
                      [&](const char *data, size_t data_length) {
                        files.back().content.append(data, data_length);
                        return true;
                      });

                  EXPECT_EQ(5u, files.size());

                  {
                    const auto &file = get_file_value(files, "text1");
                    EXPECT_TRUE(file.filename.empty());
                    EXPECT_EQ("text default", file.content);
                  }

                  {
                    const auto &file = get_file_value(files, "text2");
                    EXPECT_TRUE(file.filename.empty());
                    EXPECT_EQ("aωb", file.content);
                  }

                  {
                    const auto &file = get_file_value(files, "file1");
                    EXPECT_EQ("hello.txt", file.filename);
                    EXPECT_EQ("text/plain", file.content_type);
                    EXPECT_EQ("h\ne\n\nl\nl\no\n", file.content);
                  }

                  {
                    const auto &file = get_file_value(files, "file3");
                    EXPECT_TRUE(file.filename.empty());
                    EXPECT_EQ("application/octet-stream", file.content_type);
                    EXPECT_EQ(0u, file.content.size());
                  }
                } else {
                  std::string body;
                  content_reader([&](const char *data, size_t data_length) {
                    EXPECT_EQ(7U, data_length);
                    body.append(data, data_length);
                    return true;
                  });
                  EXPECT_EQ(body, "content");
                  res.set_content(body, "text/plain");
                }
              })
        .Put("/content_receiver",
             [&](const Request & /*req*/, Response &res,
                 const ContentReader &content_reader) {
               std::string body;
               content_reader([&](const char *data, size_t data_length) {
                 body.append(data, data_length);
                 return true;
               });
               EXPECT_EQ(body, "content");
               res.set_content(body, "text/plain");
             })
        .Patch("/content_receiver",
               [&](const Request & /*req*/, Response &res,
                   const ContentReader &content_reader) {
                 std::string body;
                 content_reader([&](const char *data, size_t data_length) {
                   body.append(data, data_length);
                   return true;
                 });
                 EXPECT_EQ(body, "content");
                 res.set_content(body, "text/plain");
               })
        .Post("/query-string-and-body",
              [&](const Request &req, Response & /*res*/) {
                ASSERT_TRUE(req.has_param("key"));
                EXPECT_EQ(req.get_param_value("key"), "value");
                EXPECT_EQ(req.body, "content");
              })
        .Get("/last-request",
             [&](const Request &req, Response & /*res*/) {
               EXPECT_EQ("close", req.get_header_value("Connection"));
             })
        .Get(R"(/redirect/(\d+))",
             [&](const Request &req, Response &res) {
               auto num = std::stoi(req.matches[1]) + 1;
               std::string url = "/redirect/" + std::to_string(num);
               res.set_redirect(url);
             })
        .Post("/binary",
              [&](const Request &req, Response &res) {
                EXPECT_EQ(4U, req.body.size());
                EXPECT_EQ("application/octet-stream",
                          req.get_header_value("Content-Type"));
                EXPECT_EQ("4", req.get_header_value("Content-Length"));
                res.set_content(req.body, "application/octet-stream");
              })
        .Put("/binary",
             [&](const Request &req, Response &res) {
               EXPECT_EQ(4U, req.body.size());
               EXPECT_EQ("application/octet-stream",
                         req.get_header_value("Content-Type"));
               EXPECT_EQ("4", req.get_header_value("Content-Length"));
               res.set_content(req.body, "application/octet-stream");
             })
        .Patch("/binary",
               [&](const Request &req, Response &res) {
                 EXPECT_EQ(4U, req.body.size());
                 EXPECT_EQ("application/octet-stream",
                           req.get_header_value("Content-Type"));
                 EXPECT_EQ("4", req.get_header_value("Content-Length"));
                 res.set_content(req.body, "application/octet-stream");
               })
        .Delete("/binary",
                [&](const Request &req, Response &res) {
                  EXPECT_EQ(4U, req.body.size());
                  EXPECT_EQ("application/octet-stream",
                            req.get_header_value("Content-Type"));
                  EXPECT_EQ("4", req.get_header_value("Content-Length"));
                  res.set_content(req.body, "application/octet-stream");
                })
#if defined(CPPHTTPLIB_ZLIB_SUPPORT) || defined(CPPHTTPLIB_BROTLI_SUPPORT)
        .Get("/compress",
             [&](const Request & /*req*/, Response &res) {
               res.set_content(
                   "12345678901234567890123456789012345678901234567890123456789"
                   "01234567890123456789012345678901234567890",
                   "text/plain");
             })
        .Get("/nocompress",
             [&](const Request & /*req*/, Response &res) {
               res.set_content(
                   "12345678901234567890123456789012345678901234567890123456789"
                   "01234567890123456789012345678901234567890",
                   "application/octet-stream");
             })
        .Post("/compress-multipart",
              [&](const Request &req, Response & /*res*/) {
                EXPECT_EQ(2u, req.files.size());
                ASSERT_TRUE(!req.has_file("???"));

                {
                  const auto &file = req.get_file_value("key1");
                  EXPECT_TRUE(file.filename.empty());
                  EXPECT_EQ("test", file.content);
                }

                {
                  const auto &file = req.get_file_value("key2");
                  EXPECT_TRUE(file.filename.empty());
                  EXPECT_EQ("--abcdefg123", file.content);
                }
              })
#endif
        ;

    persons_["john"] = "programmer";

    t_ = thread([&]() { ASSERT_TRUE(svr_.listen(HOST, PORT)); });

    while (!svr_.is_running()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  virtual void TearDown() {
    svr_.stop();
    if (!request_threads_.empty()) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      for (auto &t : request_threads_) {
        t.join();
      }
    }
    t_.join();
  }

  map<string, string> persons_;
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  SSLClient cli_;
  SSLServer svr_;
#else
  Client cli_;
  Server svr_;
#endif
  thread t_;
  std::vector<thread> request_threads_;
};

TEST_F(ServerTest, GetMethod200) {
  auto res = cli_.Get("/hi");
  ASSERT_TRUE(res);
  EXPECT_EQ("HTTP/1.1", res->version);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("OK", res->reason);
  EXPECT_EQ("text/plain", res->get_header_value("Content-Type"));
  EXPECT_EQ(1U, res->get_header_value_count("Content-Type"));
  EXPECT_EQ("Hello World!", res->body);
}

TEST_F(ServerTest, GetMethod200withPercentEncoding) {
  auto res = cli_.Get("/%68%69"); // auto res = cli_.Get("/hi");
  ASSERT_TRUE(res);
  EXPECT_EQ("HTTP/1.1", res->version);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("text/plain", res->get_header_value("Content-Type"));
  EXPECT_EQ(1U, res->get_header_value_count("Content-Type"));
  EXPECT_EQ("Hello World!", res->body);
}

TEST_F(ServerTest, GetMethod302) {
  auto res = cli_.Get("/");
  ASSERT_TRUE(res);
  EXPECT_EQ(302, res->status);
  EXPECT_EQ("/hi", res->get_header_value("Location"));
}

TEST_F(ServerTest, GetMethod302Redirect) {
  cli_.set_follow_location(true);
  auto res = cli_.Get("/");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("Hello World!", res->body);
  EXPECT_EQ("/hi", res->location);
}

TEST_F(ServerTest, GetMethod404) {
  auto res = cli_.Get("/invalid");
  ASSERT_TRUE(res);
  EXPECT_EQ(404, res->status);
}

TEST_F(ServerTest, HeadMethod200) {
  auto res = cli_.Head("/hi");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("text/plain", res->get_header_value("Content-Type"));
  EXPECT_TRUE(res->body.empty());
}

TEST_F(ServerTest, HeadMethod200Static) {
  auto res = cli_.Head("/mount/dir/index.html");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("text/html", res->get_header_value("Content-Type"));
  EXPECT_EQ(104, std::stoi(res->get_header_value("Content-Length")));
  EXPECT_TRUE(res->body.empty());
}

TEST_F(ServerTest, HeadMethod404) {
  auto res = cli_.Head("/invalid");
  ASSERT_TRUE(res);
  EXPECT_EQ(404, res->status);
  EXPECT_TRUE(res->body.empty());
}

TEST_F(ServerTest, GetMethodPersonJohn) {
  auto res = cli_.Get("/person/john");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("text/plain", res->get_header_value("Content-Type"));
  EXPECT_EQ("programmer", res->body);
}

TEST_F(ServerTest, PostMethod1) {
  auto res = cli_.Get("/person/john1");
  ASSERT_TRUE(res);
  ASSERT_EQ(404, res->status);

  res = cli_.Post("/person", "name=john1&note=coder",
                  "application/x-www-form-urlencoded");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);

  res = cli_.Get("/person/john1");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  ASSERT_EQ("text/plain", res->get_header_value("Content-Type"));
  ASSERT_EQ("coder", res->body);
}

TEST_F(ServerTest, PostMethod2) {
  auto res = cli_.Get("/person/john2");
  ASSERT_TRUE(res);
  ASSERT_EQ(404, res->status);

  Params params;
  params.emplace("name", "john2");
  params.emplace("note", "coder");

  res = cli_.Post("/person", params);
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);

  res = cli_.Get("/person/john2");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  ASSERT_EQ("text/plain", res->get_header_value("Content-Type"));
  ASSERT_EQ("coder", res->body);
}

TEST_F(ServerTest, PutMethod3) {
  auto res = cli_.Get("/person/john3");
  ASSERT_TRUE(res);
  ASSERT_EQ(404, res->status);

  Params params;
  params.emplace("name", "john3");
  params.emplace("note", "coder");

  res = cli_.Put("/person", params);
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);

  res = cli_.Get("/person/john3");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  ASSERT_EQ("text/plain", res->get_header_value("Content-Type"));
  ASSERT_EQ("coder", res->body);
}

TEST_F(ServerTest, PostWwwFormUrlEncodedJson) {
  Params params;
  params.emplace("json", JSON_DATA);

  auto res = cli_.Post("/x-www-form-urlencoded-json", params);

  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  ASSERT_EQ(JSON_DATA, res->body);
}

TEST_F(ServerTest, PostEmptyContent) {
  auto res = cli_.Post("/empty", "", "text/plain");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  ASSERT_EQ("empty", res->body);
}

TEST_F(ServerTest, PostEmptyContentWithNoContentType) {
  auto res = cli_.Post("/empty-no-content-type");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  ASSERT_EQ("empty-no-content-type", res->body);
}

TEST_F(ServerTest, PostPathOnly) {
  auto res = cli_.Post("/path-only");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  ASSERT_EQ("path-only", res->body);
}

TEST_F(ServerTest, PostPathAndHeadersOnly) {
  auto res = cli_.Post("/path-headers-only",
                       Headers({{"hello", "world"}, {"hello2", "world2"}}));
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  ASSERT_EQ("path-headers-only", res->body);
}

TEST_F(ServerTest, PostLarge) {
  auto res = cli_.Post("/post-large", LARGE_DATA, "text/plain");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  EXPECT_EQ(LARGE_DATA, res->body);
}

TEST_F(ServerTest, PutEmptyContentWithNoContentType) {
  auto res = cli_.Put("/empty-no-content-type");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  ASSERT_EQ("empty-no-content-type", res->body);
}

TEST_F(ServerTest, GetMethodDir) {
  auto res = cli_.Get("/dir/");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("text/html", res->get_header_value("Content-Type"));

  auto body = R"(<html>
<head>
</head>
<body>
  <a href="/dir/test.html">Test</a>
  <a href="/hi">hi</a>
</body>
</html>
)";
  EXPECT_EQ(body, res->body);
}

TEST_F(ServerTest, GetMethodDirTest) {
  auto res = cli_.Get("/dir/test.html");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("text/html", res->get_header_value("Content-Type"));
  EXPECT_EQ("test.html", res->body);
}

TEST_F(ServerTest, GetMethodDirTestWithDoubleDots) {
  auto res = cli_.Get("/dir/../dir/test.html");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("text/html", res->get_header_value("Content-Type"));
  EXPECT_EQ("test.html", res->body);
}

TEST_F(ServerTest, GetMethodInvalidPath) {
  auto res = cli_.Get("/dir/../test.html");
  ASSERT_TRUE(res);
  EXPECT_EQ(404, res->status);
}

TEST_F(ServerTest, GetMethodOutOfBaseDir) {
  auto res = cli_.Get("/../www/dir/test.html");
  ASSERT_TRUE(res);
  EXPECT_EQ(404, res->status);
}

TEST_F(ServerTest, GetMethodOutOfBaseDir2) {
  auto res = cli_.Get("/dir/../../www/dir/test.html");
  ASSERT_TRUE(res);
  EXPECT_EQ(404, res->status);
}

TEST_F(ServerTest, GetMethodDirMountTest) {
  auto res = cli_.Get("/mount/dir/test.html");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("text/html", res->get_header_value("Content-Type"));
  EXPECT_EQ("test.html", res->body);
}

TEST_F(ServerTest, GetMethodDirMountTestWithDoubleDots) {
  auto res = cli_.Get("/mount/dir/../dir/test.html");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("text/html", res->get_header_value("Content-Type"));
  EXPECT_EQ("test.html", res->body);
}

TEST_F(ServerTest, GetMethodInvalidMountPath) {
  auto res = cli_.Get("/mount/dir/../test.html");
  ASSERT_TRUE(res);
  EXPECT_EQ(404, res->status);
}

TEST_F(ServerTest, GetMethodOutOfBaseDirMount) {
  auto res = cli_.Get("/mount/../www2/dir/test.html");
  ASSERT_TRUE(res);
  EXPECT_EQ(404, res->status);
}

TEST_F(ServerTest, GetMethodOutOfBaseDirMount2) {
  auto res = cli_.Get("/mount/dir/../../www2/dir/test.html");
  ASSERT_TRUE(res);
  EXPECT_EQ(404, res->status);
}

TEST_F(ServerTest, PostMethod303) {
  auto res = cli_.Post("/1", "body", "text/plain");
  ASSERT_TRUE(res);
  EXPECT_EQ(303, res->status);
  EXPECT_EQ("/2", res->get_header_value("Location"));
}

TEST_F(ServerTest, PostMethod303Redirect) {
  cli_.set_follow_location(true);
  auto res = cli_.Post("/1", "body", "text/plain");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("redirected.", res->body);
  EXPECT_EQ("/2", res->location);
}

TEST_F(ServerTest, UserDefinedMIMETypeMapping) {
  auto res = cli_.Get("/dir/test.abcde");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("text/abcde", res->get_header_value("Content-Type"));
  EXPECT_EQ("abcde", res->body);
}

TEST_F(ServerTest, StaticFileRange) {
  auto res = cli_.Get("/dir/test.abcde", {{make_range_header({{2, 3}})}});
  ASSERT_TRUE(res);
  EXPECT_EQ(206, res->status);
  EXPECT_EQ("text/abcde", res->get_header_value("Content-Type"));
  EXPECT_EQ("2", res->get_header_value("Content-Length"));
  EXPECT_EQ(true, res->has_header("Content-Range"));
  EXPECT_EQ(std::string("cd"), res->body);
}

TEST_F(ServerTest, InvalidBaseDirMount) {
  EXPECT_EQ(false, svr_.set_mount_point("invalid_mount_point", "./www3"));
}

TEST_F(ServerTest, Binary) {
  std::vector<char> binary{0x00, 0x01, 0x02, 0x03};

  auto res = cli_.Post("/binary", binary.data(), binary.size(),
                       "application/octet-stream");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  ASSERT_EQ(4U, res->body.size());

  res = cli_.Put("/binary", binary.data(), binary.size(),
                 "application/octet-stream");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  ASSERT_EQ(4U, res->body.size());

  res = cli_.Patch("/binary", binary.data(), binary.size(),
                   "application/octet-stream");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  ASSERT_EQ(4U, res->body.size());

  res = cli_.Delete("/binary", binary.data(), binary.size(),
                    "application/octet-stream");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  ASSERT_EQ(4U, res->body.size());
}

TEST_F(ServerTest, BinaryString) {
  auto binary = std::string("\x00\x01\x02\x03", 4);

  auto res = cli_.Post("/binary", binary, "application/octet-stream");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  ASSERT_EQ(4U, res->body.size());

  res = cli_.Put("/binary", binary, "application/octet-stream");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  ASSERT_EQ(4U, res->body.size());

  res = cli_.Patch("/binary", binary, "application/octet-stream");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  ASSERT_EQ(4U, res->body.size());

  res = cli_.Delete("/binary", binary, "application/octet-stream");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  ASSERT_EQ(4U, res->body.size());
}

TEST_F(ServerTest, EmptyRequest) {
  auto res = cli_.Get("");
  ASSERT_TRUE(!res);
  EXPECT_EQ(Error::Connection, res.error());
}

TEST_F(ServerTest, LongRequest) {
  std::string request;
  for (size_t i = 0; i < 545; i++) {
    request += "/TooLongRequest";
  }
  request += "OK";

  auto res = cli_.Get(request.c_str());

  ASSERT_TRUE(res);
  EXPECT_EQ(404, res->status);
}

TEST_F(ServerTest, TooLongRequest) {
  std::string request;
  for (size_t i = 0; i < 545; i++) {
    request += "/TooLongRequest";
  }
  request += "_NG";

  auto res = cli_.Get(request.c_str());

  ASSERT_TRUE(res);
  EXPECT_EQ(414, res->status);
}

TEST_F(ServerTest, LongHeader) {
  Request req;
  req.method = "GET";
  req.path = "/hi";

  std::string host_and_port;
  host_and_port += HOST;
  host_and_port += ":";
  host_and_port += std::to_string(PORT);

  req.headers.emplace("Host", host_and_port.c_str());
  req.headers.emplace("Accept", "*/*");
  req.headers.emplace("User-Agent", "cpp-httplib/0.1");

  req.headers.emplace(
      "Header-Name",
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@");

  auto res = std::make_shared<Response>();
  auto error = Error::Success;
  auto ret = cli_.send(req, *res, error);

  ASSERT_TRUE(ret);
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, LongQueryValue) {
  auto res = cli_.Get(LONG_QUERY_URL.c_str());

  ASSERT_TRUE(res);
  EXPECT_EQ(414, res->status);
}

TEST_F(ServerTest, TooLongHeader) {
  Request req;
  req.method = "GET";
  req.path = "/hi";

  std::string host_and_port;
  host_and_port += HOST;
  host_and_port += ":";
  host_and_port += std::to_string(PORT);

  req.headers.emplace("Host", host_and_port.c_str());
  req.headers.emplace("Accept", "*/*");
  req.headers.emplace("User-Agent", "cpp-httplib/0.1");

  req.headers.emplace(
      "Header-Name",
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@"
      "@@@@@@@@@@@@@@@@@");

  auto res = std::make_shared<Response>();
  auto error = Error::Success;
  auto ret = cli_.send(req, *res, error);

  ASSERT_TRUE(ret);
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, PercentEncoding) {
  auto res = cli_.Get("/e%6edwith%");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, PercentEncodingUnicode) {
  auto res = cli_.Get("/e%u006edwith%");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, InvalidPercentEncoding) {
  auto res = cli_.Get("/%endwith%");
  ASSERT_TRUE(res);
  EXPECT_EQ(404, res->status);
}

TEST_F(ServerTest, InvalidPercentEncodingUnicode) {
  auto res = cli_.Get("/%uendwith%");
  ASSERT_TRUE(res);
  EXPECT_EQ(404, res->status);
}

TEST_F(ServerTest, EndWithPercentCharacterInQuery) {
  auto res = cli_.Get("/hello?aaa=bbb%");
  ASSERT_TRUE(res);
  EXPECT_EQ(404, res->status);
}

TEST_F(ServerTest, PlusSignEncoding) {
  auto res = cli_.Get("/a+%2Bb?a %2bb=a %2Bb");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("a +b", res->body);
}

TEST_F(ServerTest, MultipartFormData) {
  MultipartFormDataItems items = {
      {"text1", "text default", "", ""},
      {"text2", "aωb", "", ""},
      {"file1", "h\ne\n\nl\nl\no\n", "hello.txt", "text/plain"},
      {"file2", "{\n  \"world\", true\n}\n", "world.json", "application/json"},
      {"file3", "", "", "application/octet-stream"},
      {"file4", "", "", "   application/json  tmp-string    "}};

  auto res = cli_.Post("/multipart", items);

  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, MultipartFormDataMultiFileValues) {
  MultipartFormDataItems items = {
      {"text", "default text", "", ""},

      {"multi_text1", "aaaaa", "", ""},
      {"multi_text1", "bbbbb", "", ""},

      {"multi_file1", "h\ne\n\nl\nl\no\n", "hello.txt", "text/plain"},
      {"multi_file1", "{\n  \"world\", true\n}\n", "world.json",
       "application/json"},
  };

  auto res = cli_.Post("/multipart/multi_file_values", items);

  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, CaseInsensitiveHeaderName) {
  auto res = cli_.Get("/hi");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("text/plain", res->get_header_value("content-type"));
  EXPECT_EQ("Hello World!", res->body);
}

TEST_F(ServerTest, CaseInsensitiveTransferEncoding) {
  Request req;
  req.method = "POST";
  req.path = "/chunked";

  std::string host_and_port;
  host_and_port += HOST;
  host_and_port += ":";
  host_and_port += std::to_string(PORT);

  req.headers.emplace("Host", host_and_port.c_str());
  req.headers.emplace("Accept", "*/*");
  req.headers.emplace("User-Agent", "cpp-httplib/0.1");
  req.headers.emplace("Content-Type", "text/plain");
  req.headers.emplace("Content-Length", "0");
  req.headers.emplace(
      "Transfer-Encoding",
      "Chunked"); // Note, "Chunked" rather than typical "chunked".

  // Client does not chunk, so make a chunked body manually.
  req.body = "4\r\ndech\r\nf\r\nunked post body\r\n0\r\n\r\n";

  auto res = std::make_shared<Response>();
  auto error = Error::Success;
  auto ret = cli_.send(req, *res, error);

  ASSERT_TRUE(ret);
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, GetStreamed2) {
  auto res = cli_.Get("/streamed", {{make_range_header({{2, 3}})}});
  ASSERT_TRUE(res);
  EXPECT_EQ(206, res->status);
  EXPECT_EQ("2", res->get_header_value("Content-Length"));
  EXPECT_EQ(std::string("ab"), res->body);
}

TEST_F(ServerTest, GetStreamed) {
  auto res = cli_.Get("/streamed");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("6", res->get_header_value("Content-Length"));
  EXPECT_EQ(std::string("aaabbb"), res->body);
}

TEST_F(ServerTest, GetStreamedWithRange1) {
  auto res = cli_.Get("/streamed-with-range", {{make_range_header({{3, 5}})}});
  ASSERT_TRUE(res);
  EXPECT_EQ(206, res->status);
  EXPECT_EQ("3", res->get_header_value("Content-Length"));
  EXPECT_EQ(true, res->has_header("Content-Range"));
  EXPECT_EQ(std::string("def"), res->body);
}

TEST_F(ServerTest, GetStreamedWithRange2) {
  auto res = cli_.Get("/streamed-with-range", {{make_range_header({{1, -1}})}});
  ASSERT_TRUE(res);
  EXPECT_EQ(206, res->status);
  EXPECT_EQ("6", res->get_header_value("Content-Length"));
  EXPECT_EQ(true, res->has_header("Content-Range"));
  EXPECT_EQ(std::string("bcdefg"), res->body);
}

TEST_F(ServerTest, GetStreamedWithRangeSuffix1) {
  auto res = cli_.Get("/streamed-with-range", {{"Range", "bytes=-3"}});
  ASSERT_TRUE(res);
  EXPECT_EQ(206, res->status);
  EXPECT_EQ("3", res->get_header_value("Content-Length"));
  EXPECT_EQ(true, res->has_header("Content-Range"));
  EXPECT_EQ(std::string("efg"), res->body);
}

TEST_F(ServerTest, GetStreamedWithRangeSuffix2) {
  auto res = cli_.Get("/streamed-with-range", {{"Range", "bytes=-9999"}});
  ASSERT_TRUE(res);
  EXPECT_EQ(206, res->status);
  EXPECT_EQ("7", res->get_header_value("Content-Length"));
  EXPECT_EQ(true, res->has_header("Content-Range"));
  EXPECT_EQ(std::string("abcdefg"), res->body);
}

TEST_F(ServerTest, GetStreamedWithRangeError) {
  auto res = cli_.Get("/streamed-with-range",
                      {{"Range", "bytes=92233720368547758079223372036854775806-"
                                 "92233720368547758079223372036854775807"}});
  ASSERT_TRUE(res);
  EXPECT_EQ(416, res->status);
}

TEST_F(ServerTest, GetRangeWithMaxLongLength) {
  auto res =
      cli_.Get("/with-range", {{"Range", "bytes=0-9223372036854775807"}});
  EXPECT_EQ(206, res->status);
  EXPECT_EQ("7", res->get_header_value("Content-Length"));
  EXPECT_EQ(true, res->has_header("Content-Range"));
  EXPECT_EQ(std::string("abcdefg"), res->body);
}

TEST_F(ServerTest, GetStreamedWithRangeMultipart) {
  auto res =
      cli_.Get("/streamed-with-range", {{make_range_header({{1, 2}, {4, 5}})}});
  ASSERT_TRUE(res);
  EXPECT_EQ(206, res->status);
  EXPECT_EQ("269", res->get_header_value("Content-Length"));
  EXPECT_EQ(false, res->has_header("Content-Range"));
  EXPECT_EQ(269U, res->body.size());
}

TEST_F(ServerTest, GetStreamedEndless) {
  uint64_t offset = 0;
  auto res = cli_.Get("/streamed-cancel",
                      [&](const char * /*data*/, uint64_t data_length) {
                        if (offset < 100) {
                          offset += data_length;
                          return true;
                        }
                        return false;
                      });
  ASSERT_TRUE(!res);
  EXPECT_EQ(Error::Canceled, res.error());
}

TEST_F(ServerTest, ClientStop) {
  std::atomic_size_t count{4};
  std::vector<std::thread> threads;

  for (auto i = count.load(); i != 0; --i) {
    threads.emplace_back([&]() {
      auto res = cli_.Get("/streamed-cancel",
                          [&](const char *, uint64_t) { return true; });

      --count;

      ASSERT_TRUE(!res);
      EXPECT_TRUE(res.error() == Error::Canceled ||
                  res.error() == Error::Read || res.error() == Error::Write);
    });
  }

  std::this_thread::sleep_for(std::chrono::seconds(2));
  while (count != 0) {
    cli_.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  for (auto &t : threads) {
    t.join();
  }
}

TEST_F(ServerTest, GetWithRange1) {
  auto res = cli_.Get("/with-range", {{make_range_header({{3, 5}})}});
  ASSERT_TRUE(res);
  EXPECT_EQ(206, res->status);
  EXPECT_EQ("3", res->get_header_value("Content-Length"));
  EXPECT_EQ(true, res->has_header("Content-Range"));
  EXPECT_EQ(std::string("def"), res->body);
}

TEST_F(ServerTest, GetWithRange2) {
  auto res = cli_.Get("/with-range", {{make_range_header({{1, -1}})}});
  ASSERT_TRUE(res);
  EXPECT_EQ(206, res->status);
  EXPECT_EQ("6", res->get_header_value("Content-Length"));
  EXPECT_EQ(true, res->has_header("Content-Range"));
  EXPECT_EQ(std::string("bcdefg"), res->body);
}

TEST_F(ServerTest, GetWithRange3) {
  auto res = cli_.Get("/with-range", {{make_range_header({{0, 0}})}});
  ASSERT_TRUE(res);
  EXPECT_EQ(206, res->status);
  EXPECT_EQ("1", res->get_header_value("Content-Length"));
  EXPECT_EQ(true, res->has_header("Content-Range"));
  EXPECT_EQ(std::string("a"), res->body);
}

TEST_F(ServerTest, GetWithRange4) {
  auto res = cli_.Get("/with-range", {{make_range_header({{-1, 2}})}});
  ASSERT_TRUE(res);
  EXPECT_EQ(206, res->status);
  EXPECT_EQ("2", res->get_header_value("Content-Length"));
  EXPECT_EQ(true, res->has_header("Content-Range"));
  EXPECT_EQ(std::string("fg"), res->body);
}

TEST_F(ServerTest, GetWithRangeOffsetGreaterThanContent) {
  auto res = cli_.Get("/with-range", {{make_range_header({{10000, 20000}})}});
  ASSERT_TRUE(res);
  EXPECT_EQ(416, res->status);
}

TEST_F(ServerTest, GetWithRangeMultipart) {
  auto res = cli_.Get("/with-range", {{make_range_header({{1, 2}, {4, 5}})}});
  ASSERT_TRUE(res);
  EXPECT_EQ(206, res->status);
  EXPECT_EQ("269", res->get_header_value("Content-Length"));
  EXPECT_EQ(false, res->has_header("Content-Range"));
  EXPECT_EQ(269U, res->body.size());
}

TEST_F(ServerTest, GetWithRangeMultipartOffsetGreaterThanContent) {
  auto res =
      cli_.Get("/with-range", {{make_range_header({{-1, 2}, {10000, 30000}})}});
  ASSERT_TRUE(res);
  EXPECT_EQ(416, res->status);
}

TEST_F(ServerTest, GetStreamedChunked) {
  auto res = cli_.Get("/streamed-chunked");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ(std::string("123456789"), res->body);
}

TEST_F(ServerTest, GetStreamedChunked2) {
  auto res = cli_.Get("/streamed-chunked2");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ(std::string("123456789"), res->body);
}

TEST_F(ServerTest, GetStreamedChunkedWithTrailer) {
  auto res = cli_.Get("/streamed-chunked-with-trailer");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ(std::string("123456789"), res->body);
  EXPECT_EQ(std::string("DummyVal1"), res->get_header_value("Dummy1"));
  EXPECT_EQ(std::string("DummyVal2"), res->get_header_value("Dummy2"));
}

TEST_F(ServerTest, LargeChunkedPost) {
  Request req;
  req.method = "POST";
  req.path = "/large-chunked";

  std::string host_and_port;
  host_and_port += HOST;
  host_and_port += ":";
  host_and_port += std::to_string(PORT);

  req.headers.emplace("Host", host_and_port.c_str());
  req.headers.emplace("Accept", "*/*");
  req.headers.emplace("User-Agent", "cpp-httplib/0.1");
  req.headers.emplace("Content-Type", "text/plain");
  req.headers.emplace("Content-Length", "0");
  req.headers.emplace("Transfer-Encoding", "chunked");

  std::string long_string(30 * 1024u, 'a');
  std::string chunk = "7800\r\n" + long_string + "\r\n";

  // Attempt to make a large enough post to exceed OS buffers, to test that
  // the server handles short reads if the full chunk data isn't available.
  req.body = chunk + chunk + chunk + chunk + chunk + chunk + "0\r\n\r\n";

  auto res = std::make_shared<Response>();
  auto error = Error::Success;
  auto ret = cli_.send(req, *res, error);

  ASSERT_TRUE(ret);
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, GetMethodRemoteAddr) {
  auto res = cli_.Get("/remote_addr");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("text/plain", res->get_header_value("Content-Type"));
  EXPECT_TRUE(res->body == "::1" || res->body == "127.0.0.1");
}

TEST_F(ServerTest, GetMethodLocalAddr) {
  auto res = cli_.Get("/local_addr");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("text/plain", res->get_header_value("Content-Type"));
  EXPECT_TRUE(res->body == std::string("::1:").append(to_string(PORT)) ||
              res->body == std::string("127.0.0.1:").append(to_string(PORT)));
}

TEST_F(ServerTest, HTTPResponseSplitting) {
  auto res = cli_.Get("/http_response_splitting");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, SlowRequest) {
  request_threads_.emplace_back([this]() { auto res = cli_.Get("/slow"); });
  request_threads_.emplace_back([this]() { auto res = cli_.Get("/slow"); });
  request_threads_.emplace_back([this]() { auto res = cli_.Get("/slow"); });
}

#if 0
TEST_F(ServerTest, SlowPost) {
  char buffer[64 * 1024];
  memset(buffer, 0x42, sizeof(buffer));

  auto res = cli_.Post(
      "/slowpost", 64 * 1024 * 1024,
      [&](size_t /*offset*/, size_t /*length*/, DataSink &sink) {
        auto ret = sink.write(buffer, sizeof(buffer));
        EXPECT_TRUE(ret);
        return true;
      },
      "text/plain");

  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, SlowPostFail) {
  char buffer[64 * 1024];
  memset(buffer, 0x42, sizeof(buffer));

  cli_.set_write_timeout(std::chrono::seconds(0));
  auto res = cli_.Post(
      "/slowpost", 64 * 1024 * 1024,
      [&](size_t /*offset*/, size_t /*length*/, DataSink &sink) {
        sink.write(buffer, sizeof(buffer));
        return true;
      },
      "text/plain");

  ASSERT_TRUE(!res);
  EXPECT_EQ(Error::Write, res.error());
}
#endif

TEST_F(ServerTest, Put) {
  auto res = cli_.Put("/put", "PUT", "text/plain");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("PUT", res->body);
}

TEST_F(ServerTest, PutWithContentProvider) {
  auto res = cli_.Put(
      "/put", 3,
      [](size_t /*offset*/, size_t /*length*/, DataSink &sink) {
        sink.os << "PUT";
        return true;
      },
      "text/plain");

  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("PUT", res->body);
}

TEST_F(ServerTest, PostWithContentProviderAbort) {
  auto res = cli_.Post(
      "/post", 42,
      [](size_t /*offset*/, size_t /*length*/, DataSink & /*sink*/) {
        return false;
      },
      "text/plain");

  ASSERT_TRUE(!res);
  EXPECT_EQ(Error::Canceled, res.error());
}

TEST_F(ServerTest, PutWithContentProviderWithoutLength) {
  auto res = cli_.Put(
      "/put",
      [](size_t /*offset*/, DataSink &sink) {
        sink.os << "PUT";
        sink.done();
        return true;
      },
      "text/plain");

  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("PUT", res->body);
}

TEST_F(ServerTest, PostWithContentProviderWithoutLengthAbort) {
  auto res = cli_.Post(
      "/post", [](size_t /*offset*/, DataSink & /*sink*/) { return false; },
      "text/plain");

  ASSERT_TRUE(!res);
  EXPECT_EQ(Error::Canceled, res.error());
}

#ifdef CPPHTTPLIB_ZLIB_SUPPORT
TEST_F(ServerTest, PutWithContentProviderWithGzip) {
  cli_.set_compress(true);
  auto res = cli_.Put(
      "/put", 3,
      [](size_t /*offset*/, size_t /*length*/, DataSink &sink) {
        sink.os << "PUT";
        return true;
      },
      "text/plain");

  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("PUT", res->body);
}

TEST_F(ServerTest, PostWithContentProviderWithGzipAbort) {
  cli_.set_compress(true);
  auto res = cli_.Post(
      "/post", 42,
      [](size_t /*offset*/, size_t /*length*/, DataSink & /*sink*/) {
        return false;
      },
      "text/plain");

  ASSERT_TRUE(!res);
  EXPECT_EQ(Error::Canceled, res.error());
}

TEST_F(ServerTest, PutWithContentProviderWithoutLengthWithGzip) {
  cli_.set_compress(true);
  auto res = cli_.Put(
      "/put",
      [](size_t /*offset*/, DataSink &sink) {
        sink.os << "PUT";
        sink.done();
        return true;
      },
      "text/plain");

  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("PUT", res->body);
}

TEST_F(ServerTest, PostWithContentProviderWithoutLengthWithGzipAbort) {
  cli_.set_compress(true);
  auto res = cli_.Post(
      "/post", [](size_t /*offset*/, DataSink & /*sink*/) { return false; },
      "text/plain");

  ASSERT_TRUE(!res);
  EXPECT_EQ(Error::Canceled, res.error());
}

TEST_F(ServerTest, PutLargeFileWithGzip) {
  cli_.set_compress(true);
  auto res = cli_.Put("/put-large", LARGE_DATA, "text/plain");

  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ(LARGE_DATA, res->body);
}

TEST_F(ServerTest, PutLargeFileWithGzip2) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  std::string s = std::string("https://") + HOST + ":" + std::to_string(PORT);
  Client cli(s.c_str());
  cli.enable_server_certificate_verification(false);
#else
  std::string s = std::string("http://") + HOST + ":" + std::to_string(PORT);
  Client cli(s.c_str());
#endif
  cli.set_compress(true);

  auto res = cli.Put("/put-large", LARGE_DATA, "text/plain");

  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ(LARGE_DATA, res->body);
  EXPECT_EQ(101942u, res.get_request_header_value<uint64_t>("Content-Length"));
  EXPECT_EQ("gzip", res.get_request_header_value("Content-Encoding"));
}

TEST_F(ServerTest, PutContentWithDeflate) {
  cli_.set_compress(false);
  Headers headers;
  headers.emplace("Content-Encoding", "deflate");
  // PUT in deflate format:
  auto res = cli_.Put("/put", headers,
                      "\170\234\013\010\015\001\0\001\361\0\372", "text/plain");

  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("PUT", res->body);
}

TEST_F(ServerTest, GetStreamedChunkedWithGzip) {
  Headers headers;
  headers.emplace("Accept-Encoding", "gzip, deflate");

  auto res = cli_.Get("/streamed-chunked", headers);
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ(std::string("123456789"), res->body);
}

TEST_F(ServerTest, GetStreamedChunkedWithGzip2) {
  Headers headers;
  headers.emplace("Accept-Encoding", "gzip, deflate");

  auto res = cli_.Get("/streamed-chunked2", headers);
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ(std::string("123456789"), res->body);
}

TEST(GzipDecompressor, ChunkedDecompression) {
  std::string data;
  for (size_t i = 0; i < 32 * 1024; ++i) {
    data.push_back(static_cast<char>('a' + i % 26));
  }

  std::string compressed_data;
  {
    httplib::detail::gzip_compressor compressor;
    bool result = compressor.compress(
        data.data(), data.size(),
        /*last=*/true,
        [&](const char *compressed_data_chunk, size_t compressed_data_size) {
          compressed_data.insert(compressed_data.size(), compressed_data_chunk,
                                 compressed_data_size);
          return true;
        });
    ASSERT_TRUE(result);
  }

  std::string decompressed_data;
  {
    httplib::detail::gzip_decompressor decompressor;

    // Chunk size is chosen specifically to have a decompressed chunk size equal
    // to 16384 bytes 16384 bytes is the size of decompressor output buffer
    size_t chunk_size = 130;
    for (size_t chunk_begin = 0; chunk_begin < compressed_data.size();
         chunk_begin += chunk_size) {
      size_t current_chunk_size =
          std::min(compressed_data.size() - chunk_begin, chunk_size);
      bool result = decompressor.decompress(
          compressed_data.data() + chunk_begin, current_chunk_size,
          [&](const char *decompressed_data_chunk,
              size_t decompressed_data_chunk_size) {
            decompressed_data.insert(decompressed_data.size(),
                                     decompressed_data_chunk,
                                     decompressed_data_chunk_size);
            return true;
          });
      ASSERT_TRUE(result);
    }
  }
  ASSERT_EQ(data, decompressed_data);
}

#ifdef _WIN32
TEST(GzipDecompressor, LargeRandomData) {

  // prepare large random data that is difficult to be compressed and is
  // expected to have large size even when compressed
  std::random_device seed_gen;
  std::mt19937 random(seed_gen());
  constexpr auto large_size_byte = 4294967296UL;            // 4GiB
  constexpr auto data_size = large_size_byte + 134217728UL; // + 128MiB
  std::vector<std::uint32_t> data(data_size / sizeof(std::uint32_t));
  std::generate(data.begin(), data.end(), [&]() { return random(); });

  // compress data over 4GiB
  std::string compressed_data;
  compressed_data.reserve(large_size_byte + 536870912UL); // + 512MiB reserved
  httplib::detail::gzip_compressor compressor;
  auto result = compressor.compress(reinterpret_cast<const char *>(data.data()),
                                    data.size() * sizeof(std::uint32_t), true,
                                    [&](const char *data, size_t size) {
                                      compressed_data.insert(
                                          compressed_data.size(), data, size);
                                      return true;
                                    });
  ASSERT_TRUE(result);

  // FIXME: compressed data size is expected to be greater than 4GiB,
  // but there is no guarantee
  // ASSERT_TRUE(compressed_data.size() >= large_size_byte);

  // decompress data over 4GiB
  std::string decompressed_data;
  decompressed_data.reserve(data_size);
  httplib::detail::gzip_decompressor decompressor;
  result = decompressor.decompress(
      compressed_data.data(), compressed_data.size(),
      [&](const char *data, size_t size) {
        decompressed_data.insert(decompressed_data.size(), data, size);
        return true;
      });
  ASSERT_TRUE(result);

  // compare
  ASSERT_EQ(data_size, decompressed_data.size());
  ASSERT_TRUE(std::memcmp(data.data(), decompressed_data.data(), data_size) ==
              0);
}
#endif
#endif

#ifdef CPPHTTPLIB_BROTLI_SUPPORT
TEST_F(ServerTest, GetStreamedChunkedWithBrotli) {
  Headers headers;
  headers.emplace("Accept-Encoding", "br");

  auto res = cli_.Get("/streamed-chunked", headers);
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ(std::string("123456789"), res->body);
}

TEST_F(ServerTest, GetStreamedChunkedWithBrotli2) {
  Headers headers;
  headers.emplace("Accept-Encoding", "br");

  auto res = cli_.Get("/streamed-chunked2", headers);
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ(std::string("123456789"), res->body);
}
#endif

TEST_F(ServerTest, Patch) {
  auto res = cli_.Patch("/patch", "PATCH", "text/plain");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("PATCH", res->body);
}

TEST_F(ServerTest, Delete) {
  auto res = cli_.Delete("/delete");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("DELETE", res->body);
}

TEST_F(ServerTest, DeleteContentReceiver) {
  auto res = cli_.Delete("/delete-body", "content", "text/plain");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("content", res->body);
}

TEST_F(ServerTest, Options) {
  auto res = cli_.Options("*");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("GET, POST, HEAD, OPTIONS", res->get_header_value("Allow"));
  EXPECT_TRUE(res->body.empty());
}

TEST_F(ServerTest, URL) {
  auto res = cli_.Get("/request-target?aaa=bbb&ccc=ddd");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, ArrayParam) {
  auto res = cli_.Get("/array-param?array=value1&array=value2&array=value3");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, NoMultipleHeaders) {
  Headers headers = {{"Content-Length", "5"}};
  auto res = cli_.Post("/validate-no-multiple-headers", headers, "hello",
                       "text/plain");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, PostContentReceiver) {
  auto res = cli_.Post("/content_receiver", "content", "text/plain");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  ASSERT_EQ("content", res->body);
}

TEST_F(ServerTest, PostMultipartFileContentReceiver) {
  MultipartFormDataItems items = {
      {"text1", "text default", "", ""},
      {"text2", "aωb", "", ""},
      {"file1", "h\ne\n\nl\nl\no\n", "hello.txt", "text/plain"},
      {"file2", "{\n  \"world\", true\n}\n", "world.json", "application/json"},
      {"file3", "", "", "application/octet-stream"},
  };

  auto res = cli_.Post("/content_receiver", items);

  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, PostMultipartPlusBoundary) {
  MultipartFormDataItems items = {
      {"text1", "text default", "", ""},
      {"text2", "aωb", "", ""},
      {"file1", "h\ne\n\nl\nl\no\n", "hello.txt", "text/plain"},
      {"file2", "{\n  \"world\", true\n}\n", "world.json", "application/json"},
      {"file3", "", "", "application/octet-stream"},
  };

  auto boundary = std::string("+++++");

  std::string body;

  for (const auto &item : items) {
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"" + item.name + "\"";
    if (!item.filename.empty()) {
      body += "; filename=\"" + item.filename + "\"";
    }
    body += "\r\n";
    if (!item.content_type.empty()) {
      body += "Content-Type: " + item.content_type + "\r\n";
    }
    body += "\r\n";
    body += item.content + "\r\n";
  }
  body += "--" + boundary + "--\r\n";

  std::string content_type = "multipart/form-data; boundary=" + boundary;
  auto res = cli_.Post("/content_receiver", body, content_type.c_str());

  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, PostContentReceiverGzip) {
  cli_.set_compress(true);
  auto res = cli_.Post("/content_receiver", "content", "text/plain");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  ASSERT_EQ("content", res->body);
}

TEST_F(ServerTest, PutContentReceiver) {
  auto res = cli_.Put("/content_receiver", "content", "text/plain");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  ASSERT_EQ("content", res->body);
}

TEST_F(ServerTest, PatchContentReceiver) {
  auto res = cli_.Patch("/content_receiver", "content", "text/plain");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
  ASSERT_EQ("content", res->body);
}

TEST_F(ServerTest, PostQueryStringAndBody) {
  auto res =
      cli_.Post("/query-string-and-body?key=value", "content", "text/plain");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
}

TEST_F(ServerTest, HTTP2Magic) {
  Request req;
  req.method = "PRI";
  req.path = "*";
  req.body = "SM";

  auto res = std::make_shared<Response>();
  auto error = Error::Success;
  auto ret = cli_.send(req, *res, error);

  ASSERT_TRUE(ret);
  EXPECT_EQ(400, res->status);
}

TEST_F(ServerTest, KeepAlive) {
  auto res = cli_.Get("/hi");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("text/plain", res->get_header_value("Content-Type"));
  EXPECT_EQ("Hello World!", res->body);

  res = cli_.Get("/hi");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("text/plain", res->get_header_value("Content-Type"));
  EXPECT_EQ("Hello World!", res->body);

  res = cli_.Get("/hi");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("text/plain", res->get_header_value("Content-Type"));
  EXPECT_EQ("Hello World!", res->body);

  res = cli_.Get("/not-exist");
  ASSERT_TRUE(res);
  EXPECT_EQ(404, res->status);

  res = cli_.Post("/empty", "", "text/plain");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("text/plain", res->get_header_value("Content-Type"));
  EXPECT_EQ("empty", res->body);
  EXPECT_EQ("close", res->get_header_value("Connection"));

  res = cli_.Post(
      "/empty", 0, [&](size_t, size_t, DataSink &) { return true; },
      "text/plain");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("text/plain", res->get_header_value("Content-Type"));
  EXPECT_EQ("empty", res->body);

  cli_.set_keep_alive(false);
  res = cli_.Get("/last-request");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("close", res->get_header_value("Connection"));
}

TEST_F(ServerTest, TooManyRedirect) {
  cli_.set_follow_location(true);
  auto res = cli_.Get("/redirect/0");
  ASSERT_TRUE(!res);
  EXPECT_EQ(Error::ExceedRedirectCount, res.error());
}

#ifdef CPPHTTPLIB_ZLIB_SUPPORT
TEST_F(ServerTest, Gzip) {
  Headers headers;
  headers.emplace("Accept-Encoding", "gzip, deflate");
  auto res = cli_.Get("/compress", headers);

  ASSERT_TRUE(res);
  EXPECT_EQ("gzip", res->get_header_value("Content-Encoding"));
  EXPECT_EQ("text/plain", res->get_header_value("Content-Type"));
  EXPECT_EQ("33", res->get_header_value("Content-Length"));
  EXPECT_EQ("123456789012345678901234567890123456789012345678901234567890123456"
            "7890123456789012345678901234567890",
            res->body);
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, GzipWithoutAcceptEncoding) {
  auto res = cli_.Get("/compress");

  ASSERT_TRUE(res);
  EXPECT_TRUE(res->get_header_value("Content-Encoding").empty());
  EXPECT_EQ("text/plain", res->get_header_value("Content-Type"));
  EXPECT_EQ("100", res->get_header_value("Content-Length"));
  EXPECT_EQ("123456789012345678901234567890123456789012345678901234567890123456"
            "7890123456789012345678901234567890",
            res->body);
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, GzipWithContentReceiver) {
  Headers headers;
  headers.emplace("Accept-Encoding", "gzip, deflate");
  std::string body;
  auto res = cli_.Get("/compress", headers,
                      [&](const char *data, uint64_t data_length) {
                        EXPECT_EQ(100U, data_length);
                        body.append(data, data_length);
                        return true;
                      });

  ASSERT_TRUE(res);
  EXPECT_EQ("gzip", res->get_header_value("Content-Encoding"));
  EXPECT_EQ("text/plain", res->get_header_value("Content-Type"));
  EXPECT_EQ("33", res->get_header_value("Content-Length"));
  EXPECT_EQ("123456789012345678901234567890123456789012345678901234567890123456"
            "7890123456789012345678901234567890",
            body);
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, GzipWithoutDecompressing) {
  Headers headers;
  headers.emplace("Accept-Encoding", "gzip, deflate");

  cli_.set_decompress(false);
  auto res = cli_.Get("/compress", headers);

  ASSERT_TRUE(res);
  EXPECT_EQ("gzip", res->get_header_value("Content-Encoding"));
  EXPECT_EQ("text/plain", res->get_header_value("Content-Type"));
  EXPECT_EQ("33", res->get_header_value("Content-Length"));
  EXPECT_EQ(33U, res->body.size());
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, GzipWithContentReceiverWithoutAcceptEncoding) {
  std::string body;
  auto res = cli_.Get("/compress", [&](const char *data, uint64_t data_length) {
    EXPECT_EQ(100U, data_length);
    body.append(data, data_length);
    return true;
  });

  ASSERT_TRUE(res);
  EXPECT_TRUE(res->get_header_value("Content-Encoding").empty());
  EXPECT_EQ("text/plain", res->get_header_value("Content-Type"));
  EXPECT_EQ("100", res->get_header_value("Content-Length"));
  EXPECT_EQ("123456789012345678901234567890123456789012345678901234567890123456"
            "7890123456789012345678901234567890",
            body);
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, NoGzip) {
  Headers headers;
  headers.emplace("Accept-Encoding", "gzip, deflate");
  auto res = cli_.Get("/nocompress", headers);

  ASSERT_TRUE(res);
  EXPECT_EQ(false, res->has_header("Content-Encoding"));
  EXPECT_EQ("application/octet-stream", res->get_header_value("Content-Type"));
  EXPECT_EQ("100", res->get_header_value("Content-Length"));
  EXPECT_EQ("123456789012345678901234567890123456789012345678901234567890123456"
            "7890123456789012345678901234567890",
            res->body);
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, NoGzipWithContentReceiver) {
  Headers headers;
  headers.emplace("Accept-Encoding", "gzip, deflate");
  std::string body;
  auto res = cli_.Get("/nocompress", headers,
                      [&](const char *data, uint64_t data_length) {
                        EXPECT_EQ(100U, data_length);
                        body.append(data, data_length);
                        return true;
                      });

  ASSERT_TRUE(res);
  EXPECT_EQ(false, res->has_header("Content-Encoding"));
  EXPECT_EQ("application/octet-stream", res->get_header_value("Content-Type"));
  EXPECT_EQ("100", res->get_header_value("Content-Length"));
  EXPECT_EQ("123456789012345678901234567890123456789012345678901234567890123456"
            "7890123456789012345678901234567890",
            body);
  EXPECT_EQ(200, res->status);
}

TEST_F(ServerTest, MultipartFormDataGzip) {
  MultipartFormDataItems items = {
      {"key1", "test", "", ""},
      {"key2", "--abcdefg123", "", ""},
  };

  cli_.set_compress(true);
  auto res = cli_.Post("/compress-multipart", items);

  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}
#endif

#ifdef CPPHTTPLIB_BROTLI_SUPPORT
TEST_F(ServerTest, Brotli) {
  Headers headers;
  headers.emplace("Accept-Encoding", "br");
  auto res = cli_.Get("/compress", headers);

  ASSERT_TRUE(res);
  EXPECT_EQ("br", res->get_header_value("Content-Encoding"));
  EXPECT_EQ("text/plain", res->get_header_value("Content-Type"));
  EXPECT_EQ("19", res->get_header_value("Content-Length"));
  EXPECT_EQ("123456789012345678901234567890123456789012345678901234567890123456"
            "7890123456789012345678901234567890",
            res->body);
  EXPECT_EQ(200, res->status);
}
#endif

// Sends a raw request to a server listening at HOST:PORT.
static bool send_request(time_t read_timeout_sec, const std::string &req,
                         std::string *resp = nullptr) {
  auto error = Error::Success;

  auto client_sock = detail::create_client_socket(
      HOST, "", PORT, AF_UNSPEC, false, nullptr,
      /*connection_timeout_sec=*/5, 0,
      /*read_timeout_sec=*/5, 0,
      /*write_timeout_sec=*/5, 0, std::string(), error);

  if (client_sock == INVALID_SOCKET) { return false; }

  auto ret = detail::process_client_socket(
      client_sock, read_timeout_sec, 0, 0, 0, [&](Stream &strm) {
        if (req.size() !=
            static_cast<size_t>(strm.write(req.data(), req.size()))) {
          return false;
        }

        char buf[512];

        detail::stream_line_reader line_reader(strm, buf, sizeof(buf));
        while (line_reader.getline()) {
          if (resp) { *resp += line_reader.ptr(); }
        }
        return true;
      });

  detail::close_socket(client_sock);

  return ret;
}

TEST(ServerRequestParsingTest, TrimWhitespaceFromHeaderValues) {
  Server svr;
  std::string header_value;
  svr.Get("/validate-ws-in-headers", [&](const Request &req, Response &res) {
    header_value = req.get_header_value("foo");
    res.set_content("ok", "text/plain");
  });

  thread t = thread([&] { svr.listen(HOST, PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();

  // Only space and horizontal tab are whitespace. Make sure other whitespace-
  // like characters are not treated the same - use vertical tab and escape.
  const std::string req = "GET /validate-ws-in-headers HTTP/1.1\r\n"
                          "foo: \t \v bar \x1B\t \r\n"
                          "Connection: close\r\n"
                          "\r\n";

  ASSERT_TRUE(send_request(5, req));
  EXPECT_EQ(header_value, "\v bar \x1B");
}

// Sends a raw request and verifies that there isn't a crash or exception.
static void test_raw_request(const std::string &req,
                             std::string *out = nullptr) {
  Server svr;
  svr.Get("/hi", [&](const Request & /*req*/, Response &res) {
    res.set_content("ok", "text/plain");
  });
  svr.Put("/put_hi", [&](const Request & /*req*/, Response &res) {
    res.set_content("ok", "text/plain");
  });

  // Server read timeout must be longer than the client read timeout for the
  // bug to reproduce, probably to force the server to process a request
  // without a trailing blank line.
  const time_t client_read_timeout_sec = 1;
  svr.set_read_timeout(std::chrono::seconds(client_read_timeout_sec + 1));
  bool listen_thread_ok = false;
  thread t = thread([&] { listen_thread_ok = svr.listen(HOST, PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    t.join();
    ASSERT_FALSE(svr.is_running());
    EXPECT_TRUE(listen_thread_ok);
  });

  svr.wait_until_ready();

  ASSERT_TRUE(send_request(client_read_timeout_sec, req, out));
}

TEST(ServerRequestParsingTest, ReadHeadersRegexComplexity) {
  // A certain header line causes an exception if the header property is parsed
  // naively with a single regex. This occurs with libc++ but not libstdc++.
  test_raw_request(
      "GET /hi HTTP/1.1\r\n"
      " :                                                                      "
      "                                                                      "
      " ");
}

TEST(ServerRequestParsingTest, ReadHeadersRegexComplexity2) {
  // A certain header line causes an exception if the header property *name* is
  // parsed with a regular expression starting with "(.+?):" - this is a non-
  // greedy matcher and requires backtracking when there are a lot of ":"
  // characters.
  // This occurs with libc++ but not libstdc++.
  test_raw_request(
      "GET /hi HTTP/1.1\r\n"
      ":-:::::::::::::::::::::::::::-::::::::::::::::::::::::@-&&&&&&&&&&&"
      "--:::::::-:::::::::::::::::::::::::::::-:::::::::::::::::@-&&&&&&&&"
      "&&&--:::::::-:::::::::::::::::::::::::::::-:::::::::::::::::@-:::::"
      "::-:::::::::::::::::@-&&&&&&&&&&&--:::::::-::::::::::::::::::::::::"
      ":::::-:::::::::::::::::@-&&&&&&&&&&&--:::::::-:::::::::::::::::::::"
      "::::::::-:::::::::::::::::@-&&&&&&&--:::::::-::::::::::::::::::::::"
      ":::::::-:::::::::::::::::@-&&&&&&&&&&&--:::::::-:::::::::::::::::::"
      "::::::::::-:::::::::::::::::@-&&&&&::::::::::::-:::::::::::::::::@-"
      "&&&&&&&&&&&--:::::::-:::::::::::::::::::::::::::::-::::::::::::::::"
      ":@-&&&&&&&&&&&--:::::::-:::::::::::::::::::::::::::::-:::::::::::::"
      "::::@-&&&&&&&&&&&--:::::::-:::::::::::::::::::::::::::::-::::::@-&&"
      "&&&&&&&&&--:::::::-:::::::::::::::::::::::::::::-:::::::::::::::::@"
      "::::::-:::::::::::::::::::::::::::::-:::::::::::::::::@-&&&&&&&&&&&"
      "--:::::::-:::::::::::::::::::::::::::::-:::::::::::::::::@-&&&&&&&&"
      "&&&--:::::::-:::::::::::::::::::::::::::::-:::::::::::::::::@-&&&&&"
      "&&&&&&--:::::::-:::::::::::::::::::::::::::::-:::::::::::::::::@-&&"
      "&&&&&&&&&--:::::::-:::::::::::::::::::::::::::::-:::::::::::::::::@"
      "-&&&&&&&&&&&--:::::::-:::::::::::::::::::::::::::::-:::::::::::::::"
      "::@-&&&&&&&&&&&--:::::::-:::::::::::::::::::::::::::::-::::::::::::"
      ":::::@-&&&&&&&&&&&::-:::::::::::::::::@-&&&&&&&&&&&--:::::::-::::::"
      ":::::::::::::::::::::::-:::::::::::::::::@-&&&&&&&&&&&--:::::::-:::"
      "::::::::::::::::::::::::::-:::::::::::::::::@-&&&&&&&&&&&--:::::::-"
      ":::::::::::::::::::::::::::::-:::::::::::::::::@-&&&&&&&&&&&---&&:&"
      "&&.0------------:-:::::::::::::::::::::::::::::-:::::::::::::::::@-"
      "&&&&&&&&&&&--:::::::-:::::::::::::::::::::::::::::-::::::::::::::::"
      ":@-&&&&&&&&&&&--:::::::-:::::::::::::::::::::::::::::-:::::::::::::"
      "::::@-&&&&&&&&&&&---&&:&&&.0------------O--------\rH PUTHTTP/1.1\r\n"
      "&&&%%%");
}

TEST(ServerRequestParsingTest, ExcessiveWhitespaceInUnparsableHeaderLine) {
  // Make sure this doesn't crash the server.
  // In a previous version of the header line regex, the "\r" rendered the line
  // unparsable and the regex engine repeatedly backtracked, trying to look for
  // a new position where the leading white space ended and the field value
  // began.
  // The crash occurs with libc++ but not libstdc++.
  test_raw_request("GET /hi HTTP/1.1\r\n"
                   "a:" +
                   std::string(2000, ' ') + '\r' + std::string(20, 'z') +
                   "\r\n"
                   "\r\n");
}

TEST(ServerRequestParsingTest, InvalidFirstChunkLengthInRequest) {
  std::string out;

  test_raw_request("PUT /put_hi HTTP/1.1\r\n"
                   "Content-Type: text/plain\r\n"
                   "Transfer-Encoding: chunked\r\n"
                   "\r\n"
                   "nothex\r\n",
                   &out);
  EXPECT_EQ("HTTP/1.1 400 Bad Request", out.substr(0, 24));
}

TEST(ServerRequestParsingTest, InvalidSecondChunkLengthInRequest) {
  std::string out;

  test_raw_request("PUT /put_hi HTTP/1.1\r\n"
                   "Content-Type: text/plain\r\n"
                   "Transfer-Encoding: chunked\r\n"
                   "\r\n"
                   "3\r\n"
                   "xyz\r\n"
                   "NaN\r\n",
                   &out);
  EXPECT_EQ("HTTP/1.1 400 Bad Request", out.substr(0, 24));
}

TEST(ServerRequestParsingTest, ChunkLengthTooHighInRequest) {
  std::string out;

  test_raw_request("PUT /put_hi HTTP/1.1\r\n"
                   "Content-Type: text/plain\r\n"
                   "Transfer-Encoding: chunked\r\n"
                   "\r\n"
                   // Length is too large for 64 bits.
                   "1ffffffffffffffff\r\n"
                   "xyz\r\n",
                   &out);
  EXPECT_EQ("HTTP/1.1 400 Bad Request", out.substr(0, 24));
}

TEST(ServerRequestParsingTest, InvalidHeaderTextWithExtraCR) {
  test_raw_request("GET /hi HTTP/1.1\r\n"
                   "Content-Type: text/plain\r\n\r");
}

TEST(ServerRequestParsingTest, InvalidSpaceInURL) {
  std::string out;
  test_raw_request("GET /h i HTTP/1.1\r\n\r\n", &out);
  EXPECT_EQ("HTTP/1.1 400 Bad Request", out.substr(0, 24));
}

TEST(ServerStopTest, StopServerWithChunkedTransmission) {
  Server svr;

  svr.Get("/events", [](const Request & /*req*/, Response &res) {
    res.set_header("Cache-Control", "no-cache");
    res.set_chunked_content_provider("text/event-stream", [](size_t offset,
                                                             DataSink &sink) {
      char buffer[27];
      auto size = static_cast<size_t>(sprintf(buffer, "data:%zd\n\n", offset));
      auto ret = sink.write(buffer, size);
      EXPECT_TRUE(ret);
      std::this_thread::sleep_for(std::chrono::seconds(1));
      return true;
    });
  });

  auto listen_thread = std::thread([&svr]() { svr.listen("localhost", PORT); });
  svr.wait_until_ready();

  Client client(HOST, PORT);
  const Headers headers = {{"Accept", "text/event-stream"}};

  auto get_thread = std::thread([&client, &headers]() {
    auto res = client.Get(
        "/events", headers,
        [](const char * /*data*/, size_t /*len*/) -> bool { return true; });
  });
  auto se = detail::scope_exit([&] {
    svr.stop();
    get_thread.join();
    listen_thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  // Give GET time to get a few messages.
  std::this_thread::sleep_for(std::chrono::seconds(2));
}

TEST(ServerStopTest, ClientAccessAfterServerDown) {
  httplib::Server svr;
  svr.Post("/hi", [&](const httplib::Request & /*req*/,
                      httplib::Response &res) { res.status = 200; });

  auto thread = std::thread([&]() { svr.listen(HOST, PORT); });
  svr.wait_until_ready();

  Client cli(HOST, PORT);

  auto res = cli.Post("/hi", "data", "text/plain");

  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);

  svr.stop();
  thread.join();
  ASSERT_FALSE(svr.is_running());

  res = cli.Post("/hi", "data", "text/plain");
  ASSERT_FALSE(res);
}

TEST(ServerStopTest, ListenFailure) {
  Server svr;
  auto t = thread([&]() {
    auto ret = svr.listen("????", PORT);
    EXPECT_FALSE(ret);
  });
  svr.wait_until_ready();
  svr.stop();
  t.join();
}

TEST(StreamingTest, NoContentLengthStreaming) {
  Server svr;

  svr.Get("/stream", [](const Request & /*req*/, Response &res) {
    res.set_content_provider("text/plain", [](size_t offset, DataSink &sink) {
      if (offset < 6) {
        sink.os << (offset < 3 ? "a" : "b");
      } else {
        sink.done();
      }
      return true;
    });
  });

  auto listen_thread = std::thread([&svr]() { svr.listen("localhost", PORT); });
  auto listen_se = detail::scope_exit([&] {
    svr.stop();
    listen_thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();

  Client client(HOST, PORT);

  auto get_thread = std::thread([&client]() {
    std::string s;
    auto res =
        client.Get("/stream", [&s](const char *data, size_t len) -> bool {
          s += std::string(data, len);
          return true;
        });
    EXPECT_EQ("aaabbb", s);
  });
  auto get_se = detail::scope_exit([&] { get_thread.join(); });

  // Give GET time to get a few messages.
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

TEST(MountTest, Unmount) {
  Server svr;

  auto listen_thread = std::thread([&svr]() { svr.listen("localhost", PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    listen_thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();

  // Give GET time to get a few messages.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  Client cli("localhost", PORT);

  svr.set_mount_point("/mount2", "./www2");

  auto res = cli.Get("/");
  ASSERT_TRUE(res);
  EXPECT_EQ(404, res->status);

  res = cli.Get("/mount2/dir/test.html");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);

  svr.set_mount_point("/", "./www");

  res = cli.Get("/dir/");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);

  svr.remove_mount_point("/");
  res = cli.Get("/dir/");
  ASSERT_TRUE(res);
  EXPECT_EQ(404, res->status);

  svr.remove_mount_point("/mount2");
  res = cli.Get("/mount2/dir/test.html");
  ASSERT_TRUE(res);
  EXPECT_EQ(404, res->status);
}

#ifndef CPPHTTPLIB_NO_EXCEPTIONS
TEST(ExceptionTest, ThrowExceptionInHandler) {
  Server svr;

  svr.Get("/exception", [&](const Request & /*req*/, Response & /*res*/) {
    throw std::runtime_error("exception...");
  });

  svr.Get("/unknown", [&](const Request & /*req*/, Response & /*res*/) {
    throw std::runtime_error("exception\r\n...");
  });

  auto listen_thread = std::thread([&svr]() { svr.listen("localhost", PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    listen_thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();

  // Give GET time to get a few messages.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  Client cli("localhost", PORT);

  {
    auto res = cli.Get("/exception");
    ASSERT_TRUE(res);
    EXPECT_EQ(500, res->status);
    ASSERT_TRUE(res->has_header("EXCEPTION_WHAT"));
    EXPECT_EQ("exception...", res->get_header_value("EXCEPTION_WHAT"));
  }

  {
    auto res = cli.Get("/unknown");
    ASSERT_TRUE(res);
    EXPECT_EQ(500, res->status);
    ASSERT_TRUE(res->has_header("EXCEPTION_WHAT"));
    EXPECT_EQ("exception\\r\\n...", res->get_header_value("EXCEPTION_WHAT"));
  }
}
#endif

TEST(KeepAliveTest, ReadTimeout) {
  Server svr;

  svr.Get("/a", [&](const Request & /*req*/, Response &res) {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    res.set_content("a", "text/plain");
  });

  svr.Get("/b", [&](const Request & /*req*/, Response &res) {
    res.set_content("b", "text/plain");
  });

  auto listen_thread = std::thread([&svr]() { svr.listen("localhost", PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    listen_thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();

  // Give GET time to get a few messages.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  Client cli("localhost", PORT);
  cli.set_keep_alive(true);
  cli.set_read_timeout(std::chrono::seconds(1));

  auto resa = cli.Get("/a");
  ASSERT_FALSE(resa);
  EXPECT_EQ(Error::Read, resa.error());

  auto resb = cli.Get("/b");
  ASSERT_TRUE(resb);
  EXPECT_EQ(200, resb->status);
  EXPECT_EQ("b", resb->body);
}

TEST(KeepAliveTest, Issue1041) {
  Server svr;
  svr.set_keep_alive_timeout(3);

  svr.Get("/hi", [](const httplib::Request &, httplib::Response &res) {
    res.set_content("Hello World!", "text/plain");
  });

  auto listen_thread = std::thread([&svr] { svr.listen(HOST, PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    listen_thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  Client cli(HOST, PORT);
  cli.set_keep_alive(true);

  auto result = cli.Get("/hi");
  ASSERT_TRUE(result);
  EXPECT_EQ(200, result->status);

  std::this_thread::sleep_for(std::chrono::seconds(5));

  result = cli.Get("/hi");
  ASSERT_TRUE(result);
  EXPECT_EQ(200, result->status);
}

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
TEST(KeepAliveTest, SSLClientReconnection) {
  SSLServer svr(SERVER_CERT_FILE, SERVER_PRIVATE_KEY_FILE);
  ASSERT_TRUE(svr.is_valid());
  svr.set_keep_alive_timeout(1);

  svr.Get("/hi", [](const httplib::Request &, httplib::Response &res) {
    res.set_content("Hello World!", "text/plain");
  });

  auto listen_thread = std::thread([&svr] { svr.listen(HOST, PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    listen_thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  SSLClient cli(HOST, PORT);
  cli.enable_server_certificate_verification(false);
  cli.set_keep_alive(true);

  auto result = cli.Get("/hi");
  ASSERT_TRUE(result);
  EXPECT_EQ(200, result->status);

  result = cli.Get("/hi");
  ASSERT_TRUE(result);
  EXPECT_EQ(200, result->status);

  std::this_thread::sleep_for(std::chrono::seconds(2));

  // Recoonect
  result = cli.Get("/hi");
  ASSERT_TRUE(result);
  EXPECT_EQ(200, result->status);

  result = cli.Get("/hi");
  ASSERT_TRUE(result);
  EXPECT_EQ(200, result->status);
}
#endif

TEST(ClientProblemDetectionTest, ContentProvider) {
  Server svr;

  size_t content_length = 1024 * 1024;

  svr.Get("/hi", [&](const Request & /*req*/, Response &res) {
    res.set_content_provider(
        content_length, "text/plain",
        [&](size_t offset, size_t length, DataSink &sink) {
          auto out_len = std::min(length, static_cast<size_t>(1024));
          std::string out(out_len, '@');
          sink.write(out.data(), out_len);
          return offset < 4096;
        },
        [](bool success) { ASSERT_FALSE(success); });
  });

  svr.Get("/empty", [&](const Request & /*req*/, Response &res) {
    res.set_content_provider(
        0, "text/plain",
        [&](size_t /*offset*/, size_t /*length*/, DataSink & /*sink*/) -> bool {
          EXPECT_TRUE(false);
          return true;
        },
        [](bool success) { ASSERT_FALSE(success); });
  });

  auto listen_thread = std::thread([&svr]() { svr.listen("localhost", PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    listen_thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();

  // Give GET time to get a few messages.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  Client cli("localhost", PORT);

  {
    auto res = cli.Get("/hi", [&](const char * /*data*/,
                                  size_t /*data_length*/) { return false; });
    ASSERT_FALSE(res);
  }

  {
    auto res = cli.Get("/empty", [&](const char * /*data*/,
                                     size_t /*data_length*/) { return false; });
    ASSERT_TRUE(res);
  }
}

TEST(ErrorHandlerWithContentProviderTest, ErrorHandler) {
  Server svr;

  svr.set_error_handler([](Request const &, Response &res) -> void {
    res.set_chunked_content_provider(
        "text/plain", [](std::size_t const, DataSink &sink) -> bool {
          sink.os << "hello";
          sink.os << "world";
          sink.done();
          return true;
        });
  });

  auto listen_thread = std::thread([&svr]() { svr.listen("localhost", PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    listen_thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();

  // Give GET time to get a few messages.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  Client cli("localhost", PORT);

  auto res = cli.Get("/");
  ASSERT_TRUE(res);
  EXPECT_EQ(404, res->status);
  EXPECT_EQ("helloworld", res->body);
}

TEST(GetWithParametersTest, GetWithParameters) {
  Server svr;

  svr.Get("/", [&](const Request &req, Response &) {
    EXPECT_EQ("world", req.get_param_value("hello"));
    EXPECT_EQ("world2", req.get_param_value("hello2"));
    EXPECT_EQ("world3", req.get_param_value("hello3"));
  });

  svr.Get("/params", [&](const Request &req, Response &) {
    EXPECT_EQ("world", req.get_param_value("hello"));
    EXPECT_EQ("world2", req.get_param_value("hello2"));
    EXPECT_EQ("world3", req.get_param_value("hello3"));
  });

  svr.Get(R"(/resources/([a-z0-9\\-]+))", [&](const Request &req, Response &) {
    EXPECT_EQ("resource-id", req.matches[1]);
    EXPECT_EQ("foo", req.get_param_value("param1"));
    EXPECT_EQ("bar", req.get_param_value("param2"));
  });

  auto listen_thread = std::thread([&svr]() { svr.listen(HOST, PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    listen_thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();
  std::this_thread::sleep_for(std::chrono::seconds(1));

  {
    Client cli(HOST, PORT);

    Params params;
    params.emplace("hello", "world");
    params.emplace("hello2", "world2");
    params.emplace("hello3", "world3");
    auto res = cli.Get("/", params, Headers{});

    ASSERT_TRUE(res);
    EXPECT_EQ(200, res->status);
  }

  {
    Client cli(HOST, PORT);

    auto res = cli.Get("/params?hello=world&hello2=world2&hello3=world3");

    ASSERT_TRUE(res);
    EXPECT_EQ(200, res->status);
  }

  {
    Client cli(HOST, PORT);

    auto res = cli.Get("/resources/resource-id?param1=foo&param2=bar");

    ASSERT_TRUE(res);
    EXPECT_EQ(200, res->status);
  }
}

TEST(GetWithParametersTest, GetWithParameters2) {
  Server svr;

  svr.Get("/", [&](const Request &req, Response &res) {
    auto text = req.get_param_value("hello");
    res.set_content(text, "text/plain");
  });

  auto listen_thread = std::thread([&svr]() { svr.listen("localhost", PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    listen_thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();
  std::this_thread::sleep_for(std::chrono::seconds(1));

  Client cli("localhost", PORT);

  Params params;
  params.emplace("hello", "world");

  std::string body;
  auto res = cli.Get("/", params, Headers{},
                     [&](const char *data, size_t data_length) {
                       body.append(data, data_length);
                       return true;
                     });

  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("world", body);
}

TEST(ClientDefaultHeadersTest, DefaultHeaders_Online) {
  Client cli("httpbin.org");
  cli.set_default_headers({make_range_header({{1, 10}})});
  cli.set_connection_timeout(5);

  {
    auto res = cli.Get("/range/32");
    ASSERT_TRUE(res);
    EXPECT_EQ("bcdefghijk", res->body);
    EXPECT_EQ(206, res->status);
  }

  {
    auto res = cli.Get("/range/32");
    ASSERT_TRUE(res);
    EXPECT_EQ("bcdefghijk", res->body);
    EXPECT_EQ(206, res->status);
  }
}

TEST(ServerDefaultHeadersTest, DefaultHeaders) {
  Server svr;
  svr.set_default_headers({{"Hello", "World"}});

  svr.Get("/", [&](const Request & /*req*/, Response &res) {
    res.set_content("ok", "text/plain");
  });

  auto listen_thread = std::thread([&svr]() { svr.listen("localhost", PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    listen_thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();
  std::this_thread::sleep_for(std::chrono::seconds(1));

  Client cli("localhost", PORT);

  auto res = cli.Get("/");

  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("ok", res->body);
  EXPECT_EQ("World", res->get_header_value("Hello"));
}

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
TEST(KeepAliveTest, ReadTimeoutSSL) {
  SSLServer svr(SERVER_CERT_FILE, SERVER_PRIVATE_KEY_FILE);
  ASSERT_TRUE(svr.is_valid());

  svr.Get("/a", [&](const Request & /*req*/, Response &res) {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    res.set_content("a", "text/plain");
  });

  svr.Get("/b", [&](const Request & /*req*/, Response &res) {
    res.set_content("b", "text/plain");
  });

  auto listen_thread = std::thread([&svr]() { svr.listen("localhost", PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    listen_thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();

  // Give GET time to get a few messages.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  SSLClient cli("localhost", PORT);
  cli.enable_server_certificate_verification(false);
  cli.set_keep_alive(true);
  cli.set_read_timeout(std::chrono::seconds(1));

  auto resa = cli.Get("/a");
  ASSERT_TRUE(!resa);
  EXPECT_EQ(Error::Read, resa.error());

  auto resb = cli.Get("/b");
  ASSERT_TRUE(resb);
  EXPECT_EQ(200, resb->status);
  EXPECT_EQ("b", resb->body);
}
#endif

class ServerTestWithAI_PASSIVE : public ::testing::Test {
protected:
  ServerTestWithAI_PASSIVE()
      : cli_(HOST, PORT)
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        ,
        svr_(SERVER_CERT_FILE, SERVER_PRIVATE_KEY_FILE)
#endif
  {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    cli_.enable_server_certificate_verification(false);
#endif
  }

  virtual void SetUp() {
    svr_.Get("/hi", [&](const Request & /*req*/, Response &res) {
      res.set_content("Hello World!", "text/plain");
    });

    t_ = thread(
        [&]() { ASSERT_TRUE(svr_.listen(std::string(), PORT, AI_PASSIVE)); });

    while (!svr_.is_running()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  virtual void TearDown() {
    svr_.stop();
    t_.join();
  }

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  SSLClient cli_;
  SSLServer svr_;
#else
  Client cli_;
  Server svr_;
#endif
  thread t_;
};

TEST_F(ServerTestWithAI_PASSIVE, GetMethod200) {
  auto res = cli_.Get("/hi");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("text/plain", res->get_header_value("Content-Type"));
  EXPECT_EQ("Hello World!", res->body);
}

class ServerUpDownTest : public ::testing::Test {
protected:
  ServerUpDownTest() : cli_(HOST, PORT) {}

  virtual void SetUp() {
    t_ = thread([&]() {
      svr_.bind_to_any_port(HOST);
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      ASSERT_TRUE(svr_.listen_after_bind());
    });

    while (!svr_.is_running()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  virtual void TearDown() {
    svr_.stop();
    t_.join();
  }

  Client cli_;
  Server svr_;
  thread t_;
};

TEST_F(ServerUpDownTest, QuickStartStop) {
  // Should not crash, especially when run with
  // --gtest_filter=ServerUpDownTest.QuickStartStop --gtest_repeat=1000
}

class PayloadMaxLengthTest : public ::testing::Test {
protected:
  PayloadMaxLengthTest()
      : cli_(HOST, PORT)
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        ,
        svr_(SERVER_CERT_FILE, SERVER_PRIVATE_KEY_FILE)
#endif
  {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    cli_.enable_server_certificate_verification(false);
#endif
  }

  virtual void SetUp() {
    svr_.set_payload_max_length(8);

    svr_.Post("/test", [&](const Request & /*req*/, Response &res) {
      res.set_content("test", "text/plain");
    });

    t_ = thread([&]() { ASSERT_TRUE(svr_.listen(HOST, PORT)); });

    while (!svr_.is_running()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  virtual void TearDown() {
    svr_.stop();
    t_.join();
  }

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  SSLClient cli_;
  SSLServer svr_;
#else
  Client cli_;
  Server svr_;
#endif
  thread t_;
};

TEST_F(PayloadMaxLengthTest, ExceedLimit) {
  auto res = cli_.Post("/test", "123456789", "text/plain");
  ASSERT_TRUE(res);
  EXPECT_EQ(413, res->status);

  res = cli_.Post("/test", "12345678", "text/plain");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
TEST(SSLClientTest, UpdateCAStore) {
  httplib::SSLClient httplib_client("www.google.com");
  auto ca_store_1 = X509_STORE_new();
  X509_STORE_load_locations(ca_store_1, "/etc/ssl/certs/ca-certificates.crt",
                            nullptr);
  httplib_client.set_ca_cert_store(ca_store_1);

  auto ca_store_2 = X509_STORE_new();
  X509_STORE_load_locations(ca_store_2, "/etc/ssl/certs/ca-certificates.crt",
                            nullptr);
  httplib_client.set_ca_cert_store(ca_store_2);
}

TEST(SSLClientTest, ServerNameIndication_Online) {
  SSLClient cli("httpbin.org", 443);
  auto res = cli.Get("/get");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
}

TEST(SSLClientTest, ServerCertificateVerification1_Online) {
  Client cli("https://google.com");
  auto res = cli.Get("/");
  ASSERT_TRUE(res);
  ASSERT_EQ(301, res->status);
}

TEST(SSLClientTest, ServerCertificateVerification2_Online) {
  SSLClient cli("google.com");
  cli.enable_server_certificate_verification(true);
  cli.set_ca_cert_path("hello");
  auto res = cli.Get("/");
  ASSERT_TRUE(!res);
  EXPECT_EQ(Error::SSLLoadingCerts, res.error());
}

TEST(SSLClientTest, ServerCertificateVerification3_Online) {
  SSLClient cli("google.com");
  cli.set_ca_cert_path(CA_CERT_FILE);
  auto res = cli.Get("/");
  ASSERT_TRUE(res);
  ASSERT_EQ(301, res->status);
}

TEST(SSLClientTest, ServerCertificateVerification4) {
  SSLServer svr(SERVER_CERT2_FILE, SERVER_PRIVATE_KEY_FILE);
  ASSERT_TRUE(svr.is_valid());

  svr.Get("/test", [&](const Request &, Response &res) {
    res.set_content("test", "text/plain");
    svr.stop();
    ASSERT_TRUE(true);
  });

  thread t = thread([&]() { ASSERT_TRUE(svr.listen("127.0.0.1", PORT)); });
  auto se = detail::scope_exit([&] {
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  SSLClient cli("127.0.0.1", PORT);
  cli.set_ca_cert_path(SERVER_CERT2_FILE);
  cli.enable_server_certificate_verification(true);
  cli.set_connection_timeout(30);

  auto res = cli.Get("/test");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
}

TEST(SSLClientTest, WildcardHostNameMatch_Online) {
  SSLClient cli("www.youtube.com");

  cli.set_ca_cert_path(CA_CERT_FILE);
  cli.enable_server_certificate_verification(true);
  cli.set_follow_location(true);

  auto res = cli.Get("/");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
}

#if 0
TEST(SSLClientTest, SetInterfaceWithINET6) {
  auto cli = std::make_shared<httplib::Client>("https://httpbin.org");
  ASSERT_TRUE(cli != nullptr);

  cli->set_address_family(AF_INET6);
  cli->set_interface("en0");

  auto res = cli->Get("/get");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
}
#endif

TEST(SSLClientServerTest, ClientCertPresent) {
  SSLServer svr(SERVER_CERT_FILE, SERVER_PRIVATE_KEY_FILE, CLIENT_CA_CERT_FILE,
                CLIENT_CA_CERT_DIR);
  ASSERT_TRUE(svr.is_valid());

  svr.Get("/test", [&](const Request &req, Response &res) {
    res.set_content("test", "text/plain");
    svr.stop();
    ASSERT_TRUE(true);

    auto peer_cert = SSL_get_peer_certificate(req.ssl);
    ASSERT_TRUE(peer_cert != nullptr);

    auto subject_name = X509_get_subject_name(peer_cert);
    ASSERT_TRUE(subject_name != nullptr);

    std::string common_name;
    {
      char name[BUFSIZ];
      auto name_len = X509_NAME_get_text_by_NID(subject_name, NID_commonName,
                                                name, sizeof(name));
      common_name.assign(name, static_cast<size_t>(name_len));
    }

    EXPECT_EQ("Common Name", common_name);

    X509_free(peer_cert);
  });

  thread t = thread([&]() { ASSERT_TRUE(svr.listen(HOST, PORT)); });
  auto se = detail::scope_exit([&] {
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  SSLClient cli(HOST, PORT, CLIENT_CERT_FILE, CLIENT_PRIVATE_KEY_FILE);
  cli.enable_server_certificate_verification(false);
  cli.set_connection_timeout(30);

  auto res = cli.Get("/test");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
}

#if !defined(_WIN32) || defined(OPENSSL_USE_APPLINK)
TEST(SSLClientServerTest, MemoryClientCertPresent) {
  X509 *server_cert;
  EVP_PKEY *server_private_key;
  X509_STORE *client_ca_cert_store;
  X509 *client_cert;
  EVP_PKEY *client_private_key;

  FILE *f = fopen(SERVER_CERT_FILE, "r+");
  server_cert = PEM_read_X509(f, nullptr, nullptr, nullptr);
  fclose(f);

  f = fopen(SERVER_PRIVATE_KEY_FILE, "r+");
  server_private_key = PEM_read_PrivateKey(f, nullptr, nullptr, nullptr);
  fclose(f);

  f = fopen(CLIENT_CA_CERT_FILE, "r+");
  client_cert = PEM_read_X509(f, nullptr, nullptr, nullptr);
  client_ca_cert_store = X509_STORE_new();
  X509_STORE_add_cert(client_ca_cert_store, client_cert);
  X509_free(client_cert);
  fclose(f);

  f = fopen(CLIENT_CERT_FILE, "r+");
  client_cert = PEM_read_X509(f, nullptr, nullptr, nullptr);
  fclose(f);

  f = fopen(CLIENT_PRIVATE_KEY_FILE, "r+");
  client_private_key = PEM_read_PrivateKey(f, nullptr, nullptr, nullptr);
  fclose(f);

  SSLServer svr(server_cert, server_private_key, client_ca_cert_store);
  ASSERT_TRUE(svr.is_valid());

  svr.Get("/test", [&](const Request &req, Response &res) {
    res.set_content("test", "text/plain");
    svr.stop();
    ASSERT_TRUE(true);

    auto peer_cert = SSL_get_peer_certificate(req.ssl);
    ASSERT_TRUE(peer_cert != nullptr);

    auto subject_name = X509_get_subject_name(peer_cert);
    ASSERT_TRUE(subject_name != nullptr);

    std::string common_name;
    {
      char name[BUFSIZ];
      auto name_len = X509_NAME_get_text_by_NID(subject_name, NID_commonName,
                                                name, sizeof(name));
      common_name.assign(name, static_cast<size_t>(name_len));
    }

    EXPECT_EQ("Common Name", common_name);

    X509_free(peer_cert);
  });

  thread t = thread([&]() { ASSERT_TRUE(svr.listen(HOST, PORT)); });
  auto se = detail::scope_exit([&] {
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  SSLClient cli(HOST, PORT, client_cert, client_private_key);
  cli.enable_server_certificate_verification(false);
  cli.set_connection_timeout(30);

  auto res = cli.Get("/test");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);

  X509_free(server_cert);
  EVP_PKEY_free(server_private_key);
  X509_free(client_cert);
  EVP_PKEY_free(client_private_key);
}
#endif

TEST(SSLClientServerTest, ClientCertMissing) {
  SSLServer svr(SERVER_CERT_FILE, SERVER_PRIVATE_KEY_FILE, CLIENT_CA_CERT_FILE,
                CLIENT_CA_CERT_DIR);
  ASSERT_TRUE(svr.is_valid());

  svr.Get("/test", [&](const Request &, Response &) { ASSERT_TRUE(false); });

  thread t = thread([&]() { ASSERT_TRUE(svr.listen(HOST, PORT)); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  SSLClient cli(HOST, PORT);
  auto res = cli.Get("/test");
  cli.set_connection_timeout(30);
  ASSERT_TRUE(!res);
  EXPECT_EQ(Error::SSLServerVerification, res.error());
}

TEST(SSLClientServerTest, TrustDirOptional) {
  SSLServer svr(SERVER_CERT_FILE, SERVER_PRIVATE_KEY_FILE, CLIENT_CA_CERT_FILE);
  ASSERT_TRUE(svr.is_valid());

  svr.Get("/test", [&](const Request &, Response &res) {
    res.set_content("test", "text/plain");
    svr.stop();
  });

  thread t = thread([&]() { ASSERT_TRUE(svr.listen(HOST, PORT)); });
  auto se = detail::scope_exit([&] {
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  SSLClient cli(HOST, PORT, CLIENT_CERT_FILE, CLIENT_PRIVATE_KEY_FILE);
  cli.enable_server_certificate_verification(false);
  cli.set_connection_timeout(30);

  auto res = cli.Get("/test");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
}

TEST(SSLClientServerTest, SSLConnectTimeout) {
  class NoListenSSLServer : public SSLServer {
  public:
    NoListenSSLServer(const char *cert_path, const char *private_key_path,
                      const char *client_ca_cert_file_path,
                      const char *client_ca_cert_dir_path = nullptr)
        : SSLServer(cert_path, private_key_path, client_ca_cert_file_path,
                    client_ca_cert_dir_path),
          stop_(false) {}

    bool stop_;

  private:
    bool process_and_close_socket(socket_t /*sock*/) override {
      // Don't create SSL context
      while (!stop_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      return true;
    }
  };
  NoListenSSLServer svr(SERVER_CERT_FILE, SERVER_PRIVATE_KEY_FILE,
                        CLIENT_CA_CERT_FILE);
  ASSERT_TRUE(svr.is_valid());

  svr.Get("/test", [&](const Request &, Response &res) {
    res.set_content("test", "text/plain");
  });

  thread t = thread([&]() { ASSERT_TRUE(svr.listen(HOST, PORT)); });
  auto se = detail::scope_exit([&] {
    svr.stop_ = true;
    svr.stop();
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  SSLClient cli(HOST, PORT, CLIENT_CERT_FILE, CLIENT_PRIVATE_KEY_FILE);
  cli.enable_server_certificate_verification(false);
  cli.set_connection_timeout(1);

  auto res = cli.Get("/test");
  ASSERT_TRUE(!res);
  EXPECT_EQ(Error::SSLConnection, res.error());
}

TEST(SSLClientServerTest, CustomizeServerSSLCtx) {
  auto setup_ssl_ctx_callback = [](SSL_CTX &ssl_ctx) {
    SSL_CTX_set_options(&ssl_ctx, SSL_OP_NO_COMPRESSION);
    SSL_CTX_set_options(&ssl_ctx,
                        SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION);
    SSL_CTX_set_options(&ssl_ctx, SSL_OP_NO_SSLv2);
    SSL_CTX_set_options(&ssl_ctx, SSL_OP_NO_SSLv3);
    SSL_CTX_set_options(&ssl_ctx, SSL_OP_NO_TLSv1);
    SSL_CTX_set_options(&ssl_ctx, SSL_OP_NO_TLSv1_1);
    auto ciphers = "ECDHE-RSA-AES128-SHA256:"
                   "ECDHE-DSS-AES128-SHA256:"
                   "ECDHE-RSA-AES256-SHA256:"
                   "ECDHE-DSS-AES256-SHA256:";
    SSL_CTX_set_cipher_list(&ssl_ctx, ciphers);
    if (SSL_CTX_use_certificate_chain_file(&ssl_ctx, SERVER_CERT_FILE) != 1 ||
        SSL_CTX_use_PrivateKey_file(&ssl_ctx, SERVER_PRIVATE_KEY_FILE,
                                    SSL_FILETYPE_PEM) != 1) {
      return false;
    }
    SSL_CTX_load_verify_locations(&ssl_ctx, CLIENT_CA_CERT_FILE,
                                  CLIENT_CA_CERT_DIR);
    SSL_CTX_set_verify(
        &ssl_ctx,
        SSL_VERIFY_PEER |
            SSL_VERIFY_FAIL_IF_NO_PEER_CERT, // SSL_VERIFY_CLIENT_ONCE,
        nullptr);
    return true;
  };
  SSLServer svr(setup_ssl_ctx_callback);
  ASSERT_TRUE(svr.is_valid());

  svr.Get("/test", [&](const Request &req, Response &res) {
    res.set_content("test", "text/plain");
    svr.stop();
    ASSERT_TRUE(true);

    auto peer_cert = SSL_get_peer_certificate(req.ssl);
    ASSERT_TRUE(peer_cert != nullptr);

    auto subject_name = X509_get_subject_name(peer_cert);
    ASSERT_TRUE(subject_name != nullptr);

    std::string common_name;
    {
      char name[BUFSIZ];
      auto name_len = X509_NAME_get_text_by_NID(subject_name, NID_commonName,
                                                name, sizeof(name));
      common_name.assign(name, static_cast<size_t>(name_len));
    }

    EXPECT_EQ("Common Name", common_name);

    X509_free(peer_cert);
  });

  thread t = thread([&]() { ASSERT_TRUE(svr.listen(HOST, PORT)); });
  auto se = detail::scope_exit([&] {
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  SSLClient cli(HOST, PORT, CLIENT_CERT_FILE, CLIENT_PRIVATE_KEY_FILE);
  cli.enable_server_certificate_verification(false);
  cli.set_connection_timeout(30);

  auto res = cli.Get("/test");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
}

// Disabled due to the out-of-memory problem on GitHub Actions Workflows
TEST(SSLClientServerTest, DISABLED_LargeDataTransfer) {

  // prepare large data
  std::random_device seed_gen;
  std::mt19937 random(seed_gen());
  constexpr auto large_size_byte = 2147483648UL + 1048576UL; // 2GiB + 1MiB
  std::vector<std::uint32_t> binary(large_size_byte / sizeof(std::uint32_t));
  std::generate(binary.begin(), binary.end(), [&random]() { return random(); });

  // server
  SSLServer svr(SERVER_CERT_FILE, SERVER_PRIVATE_KEY_FILE);
  ASSERT_TRUE(svr.is_valid());

  svr.Post("/binary", [&](const Request &req, Response &res) {
    EXPECT_EQ(large_size_byte, req.body.size());
    EXPECT_EQ(0, std::memcmp(binary.data(), req.body.data(), large_size_byte));
    res.set_content(req.body, "application/octet-stream");
  });

  auto listen_thread = std::thread([&svr]() { svr.listen("localhost", PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    listen_thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();

  // client POST
  SSLClient cli("localhost", PORT);
  cli.enable_server_certificate_verification(false);
  cli.set_read_timeout(std::chrono::seconds(100));
  cli.set_write_timeout(std::chrono::seconds(100));
  auto res = cli.Post("/binary", reinterpret_cast<char *>(binary.data()),
                      large_size_byte, "application/octet-stream");

  // compare
  EXPECT_EQ(200, res->status);
  EXPECT_EQ(large_size_byte, res->body.size());
  EXPECT_EQ(0, std::memcmp(binary.data(), res->body.data(), large_size_byte));
}
#endif

#ifdef _WIN32
TEST(CleanupTest, WSACleanup) {
  int ret = WSACleanup();
  ASSERT_EQ(0, ret);
}
#endif

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
TEST(NoSSLSupport, SimpleInterface) {
  ASSERT_ANY_THROW(Client cli("https://yahoo.com"));
}
#endif

#ifndef CPPHTTPLIB_NO_EXCEPTIONS
TEST(InvalidScheme, SimpleInterface) {
  ASSERT_ANY_THROW(Client cli("scheme://yahoo.com"));
}
#endif

TEST(NoScheme, SimpleInterface) {
  Client cli("yahoo.com:80");
  ASSERT_TRUE(cli.is_valid());
}

TEST(SendAPI, SimpleInterface_Online) {
  Client cli("http://yahoo.com");

  Request req;
  req.method = "GET";
  req.path = "/";
  auto res = cli.send(req);

  ASSERT_TRUE(res);
  EXPECT_EQ(301, res->status);
}

TEST(ClientImplMethods, GetSocketTest) {
  httplib::Server svr;
  svr.Get("/", [&](const httplib::Request & /*req*/, httplib::Response &res) {
    res.status = 200;
  });

  auto thread = std::thread([&]() { svr.listen("127.0.0.1", 3333); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();

  {
    httplib::Client cli("http://127.0.0.1:3333");
    cli.set_keep_alive(true);

    // Use the behavior of cpp-httplib of opening the connection
    // only when the first request happens. If that changes,
    // this test would be obsolete.

    EXPECT_EQ(cli.socket(), INVALID_SOCKET);

    // This also implicitly tests the server. But other tests would fail much
    // earlier than this one to be considered.

    auto res = cli.Get("/");
    ASSERT_TRUE(res);

    EXPECT_EQ(200, res->status);
    ASSERT_TRUE(cli.socket() != INVALID_SOCKET);
  }
}

// Disabled due to out-of-memory problem on GitHub Actions
#ifdef _WIN64
TEST(ServerLargeContentTest, DISABLED_SendLargeContent) {
  // allocate content size larger than 2GB in memory
  const size_t content_size = 2LL * 1024LL * 1024LL * 1024LL + 1LL;
  char *content = (char *)malloc(content_size);
  ASSERT_TRUE(content);

  Server svr;
  svr.Get("/foo", [=](const httplib::Request &req, httplib::Response &resp) {
    resp.set_content(content, content_size, "application/octet-stream");
  });

  auto listen_thread = std::thread([&svr]() { svr.listen(HOST, PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    listen_thread.join();
    if (content) free(content);
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();

  // Give GET time to get a few messages.
  std::this_thread::sleep_for(std::chrono::seconds(1));

  Client cli(HOST, PORT);
  auto res = cli.Get("/foo");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ(content_size, res->body.length());
}
#endif

#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
TEST(YahooRedirectTest2, SimpleInterface_Online) {
  Client cli("http://yahoo.com");

  auto res = cli.Get("/");
  ASSERT_TRUE(res);
  EXPECT_EQ(301, res->status);

  cli.set_follow_location(true);
  res = cli.Get("/");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("https://yahoo.com/", res->location);
}

TEST(YahooRedirectTest3, SimpleInterface_Online) {
  Client cli("https://yahoo.com");

  auto res = cli.Get("/");
  ASSERT_TRUE(res);
  EXPECT_EQ(301, res->status);

  cli.set_follow_location(true);
  res = cli.Get("/");
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("https://www.yahoo.com/", res->location);
}

TEST(YahooRedirectTest3, NewResultInterface_Online) {
  Client cli("https://yahoo.com");

  auto res = cli.Get("/");
  ASSERT_TRUE(res);
  ASSERT_FALSE(!res);
  ASSERT_TRUE(res);
  ASSERT_FALSE(res == nullptr);
  ASSERT_TRUE(res != nullptr);
  EXPECT_EQ(Error::Success, res.error());
  EXPECT_EQ(301, res.value().status);
  EXPECT_EQ(301, (*res).status);
  EXPECT_EQ(301, res->status);

  cli.set_follow_location(true);
  res = cli.Get("/");
  ASSERT_TRUE(res);
  EXPECT_EQ(Error::Success, res.error());
  EXPECT_EQ(200, res.value().status);
  EXPECT_EQ(200, (*res).status);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ("https://www.yahoo.com/", res->location);
}

#ifdef CPPHTTPLIB_BROTLI_SUPPORT
TEST(DecodeWithChunkedEncoding, BrotliEncoding_Online) {
  Client cli("https://cdnjs.cloudflare.com");
  auto res =
      cli.Get("/ajax/libs/jquery/3.5.1/jquery.js", {{"Accept-Encoding", "br"}});

  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
  EXPECT_EQ(287630U, res->body.size());
  EXPECT_EQ("application/javascript; charset=utf-8",
            res->get_header_value("Content-Type"));
}
#endif

TEST(HttpsToHttpRedirectTest, SimpleInterface_Online) {
  Client cli("https://nghttp2.org");
  cli.set_follow_location(true);
  auto res =
      cli.Get("/httpbin/"
              "redirect-to?url=http%3A%2F%2Fwww.google.com&status_code=302");

  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

TEST(HttpsToHttpRedirectTest2, SimpleInterface_Online) {
  Client cli("https://nghttp2.org");
  cli.set_follow_location(true);

  Params params;
  params.emplace("url", "http://www.google.com");
  params.emplace("status_code", "302");

  auto res = cli.Get("/httpbin/redirect-to", params, Headers{});
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

TEST(HttpsToHttpRedirectTest3, SimpleInterface_Online) {
  Client cli("https://nghttp2.org");
  cli.set_follow_location(true);

  Params params;
  params.emplace("url", "http://www.google.com");

  auto res = cli.Get("/httpbin/redirect-to?status_code=302", params, Headers{});
  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

TEST(HttpToHttpsRedirectTest, CertFile) {
  Server svr;
  ASSERT_TRUE(svr.is_valid());
  svr.Get("/index", [&](const Request &, Response &res) {
    res.set_redirect("https://127.0.0.1:1235/index");
    svr.stop();
  });

  SSLServer ssl_svr(SERVER_CERT2_FILE, SERVER_PRIVATE_KEY_FILE);
  ASSERT_TRUE(ssl_svr.is_valid());
  ssl_svr.Get("/index", [&](const Request &, Response &res) {
    res.set_content("test", "text/plain");
    ssl_svr.stop();
  });

  thread t = thread([&]() { ASSERT_TRUE(svr.listen("127.0.0.1", PORT)); });
  thread t2 = thread([&]() { ASSERT_TRUE(ssl_svr.listen("127.0.0.1", 1235)); });
  auto se = detail::scope_exit([&] {
    t2.join();
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  Client cli("127.0.0.1", PORT);
  cli.set_ca_cert_path(SERVER_CERT2_FILE);
  cli.enable_server_certificate_verification(true);
  cli.set_follow_location(true);
  cli.set_connection_timeout(30);

  auto res = cli.Get("/index");
  ASSERT_TRUE(res);
  ASSERT_EQ(200, res->status);
}

TEST(MultipartFormDataTest, LargeData) {
  SSLServer svr(SERVER_CERT_FILE, SERVER_PRIVATE_KEY_FILE);

  svr.Post("/post", [&](const Request &req, Response & /*res*/,
                        const ContentReader &content_reader) {
    if (req.is_multipart_form_data()) {
      MultipartFormDataItems files;
      content_reader(
          [&](const MultipartFormData &file) {
            files.push_back(file);
            return true;
          },
          [&](const char *data, size_t data_length) {
            files.back().content.append(data, data_length);
            return true;
          });

      EXPECT_TRUE(std::string(files[0].name) == "document");
      EXPECT_EQ(size_t(1024 * 1024 * 2), files[0].content.size());
      EXPECT_TRUE(files[0].filename == "2MB_data");
      EXPECT_TRUE(files[0].content_type == "application/octet-stream");

      EXPECT_TRUE(files[1].name == "hello");
      EXPECT_TRUE(files[1].content == "world");
      EXPECT_TRUE(files[1].filename == "");
      EXPECT_TRUE(files[1].content_type == "");
    } else {
      std::string body;
      content_reader([&](const char *data, size_t data_length) {
        body.append(data, data_length);
        return true;
      });
    }
  });

  auto t = std::thread([&]() { svr.listen("localhost", 8080); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();
  std::this_thread::sleep_for(std::chrono::seconds(1));

  {
    std::string data(1024 * 1024 * 2, '.');
    std::stringstream buffer;
    buffer << data;

    Client cli("https://localhost:8080");
    cli.enable_server_certificate_verification(false);

    MultipartFormDataItems items{
        {"document", buffer.str(), "2MB_data", "application/octet-stream"},
        {"hello", "world", "", ""},
    };

    auto res = cli.Post("/post", items);
    ASSERT_TRUE(res);
    ASSERT_EQ(200, res->status);
  }
}

TEST(MultipartFormDataTest, DataProviderItems) {

  std::random_device seed_gen;
  std::mt19937 random(seed_gen());

  std::string rand1;
  rand1.resize(1000);
  std::generate(rand1.begin(), rand1.end(), [&]() { return random(); });

  std::string rand2;
  rand2.resize(3000);
  std::generate(rand2.begin(), rand2.end(), [&]() { return random(); });

  SSLServer svr(SERVER_CERT_FILE, SERVER_PRIVATE_KEY_FILE);

  svr.Post("/post-none", [&](const Request &req, Response & /*res*/,
                             const ContentReader &content_reader) {
    ASSERT_FALSE(req.is_multipart_form_data());

    std::string body;
    content_reader([&](const char *data, size_t data_length) {
      body.append(data, data_length);
      return true;
    });

    EXPECT_EQ(body, "");
  });

  svr.Post("/post-items", [&](const Request &req, Response & /*res*/,
                              const ContentReader &content_reader) {
    ASSERT_TRUE(req.is_multipart_form_data());
    MultipartFormDataItems files;
    content_reader(
        [&](const MultipartFormData &file) {
          files.push_back(file);
          return true;
        },
        [&](const char *data, size_t data_length) {
          files.back().content.append(data, data_length);
          return true;
        });

    ASSERT_TRUE(files.size() == 2);

    EXPECT_EQ(std::string(files[0].name), "name1");
    EXPECT_EQ(files[0].content, "Testing123");
    EXPECT_EQ(files[0].filename, "filename1");
    EXPECT_EQ(files[0].content_type, "application/octet-stream");

    EXPECT_EQ(files[1].name, "name2");
    EXPECT_EQ(files[1].content, "Testing456");
    EXPECT_EQ(files[1].filename, "");
    EXPECT_EQ(files[1].content_type, "");
  });

  svr.Post("/post-providers", [&](const Request &req, Response & /*res*/,
                                  const ContentReader &content_reader) {
    ASSERT_TRUE(req.is_multipart_form_data());
    MultipartFormDataItems files;
    content_reader(
        [&](const MultipartFormData &file) {
          files.push_back(file);
          return true;
        },
        [&](const char *data, size_t data_length) {
          files.back().content.append(data, data_length);
          return true;
        });

    ASSERT_TRUE(files.size() == 2);

    EXPECT_EQ(files[0].name, "name3");
    EXPECT_EQ(files[0].content, rand1);
    EXPECT_EQ(files[0].filename, "filename3");
    EXPECT_EQ(files[0].content_type, "");

    EXPECT_EQ(files[1].name, "name4");
    EXPECT_EQ(files[1].content, rand2);
    EXPECT_EQ(files[1].filename, "filename4");
    EXPECT_EQ(files[1].content_type, "");
  });

  svr.Post("/post-both", [&](const Request &req, Response & /*res*/,
                             const ContentReader &content_reader) {
    ASSERT_TRUE(req.is_multipart_form_data());
    MultipartFormDataItems files;
    content_reader(
        [&](const MultipartFormData &file) {
          files.push_back(file);
          return true;
        },
        [&](const char *data, size_t data_length) {
          files.back().content.append(data, data_length);
          return true;
        });

    ASSERT_TRUE(files.size() == 4);

    EXPECT_EQ(std::string(files[0].name), "name1");
    EXPECT_EQ(files[0].content, "Testing123");
    EXPECT_EQ(files[0].filename, "filename1");
    EXPECT_EQ(files[0].content_type, "application/octet-stream");

    EXPECT_EQ(files[1].name, "name2");
    EXPECT_EQ(files[1].content, "Testing456");
    EXPECT_EQ(files[1].filename, "");
    EXPECT_EQ(files[1].content_type, "");

    EXPECT_EQ(files[2].name, "name3");
    EXPECT_EQ(files[2].content, rand1);
    EXPECT_EQ(files[2].filename, "filename3");
    EXPECT_EQ(files[2].content_type, "");

    EXPECT_EQ(files[3].name, "name4");
    EXPECT_EQ(files[3].content, rand2);
    EXPECT_EQ(files[3].filename, "filename4");
    EXPECT_EQ(files[3].content_type, "");
  });

  auto t = std::thread([&]() { svr.listen("localhost", 8080); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();
  std::this_thread::sleep_for(std::chrono::seconds(1));

  {
    Client cli("https://localhost:8080");
    cli.enable_server_certificate_verification(false);

    MultipartFormDataItems items{
        {"name1", "Testing123", "filename1", "application/octet-stream"},
        {"name2", "Testing456", "", ""}, // not a file
    };

    {
      auto res = cli.Post("/post-none", {}, {}, {});
      ASSERT_TRUE(res);
      ASSERT_EQ(200, res->status);
    }

    MultipartFormDataProviderItems providers;

    {
      auto res =
          cli.Post("/post-items", {}, items, providers); // empty providers
      ASSERT_TRUE(res);
      ASSERT_EQ(200, res->status);
    }

    providers.push_back({"name3",
                         [&](size_t offset, httplib::DataSink &sink) -> bool {
                           // test the offset is given correctly at each step
                           if (!offset)
                             sink.os.write(rand1.data(), 30);
                           else if (offset == 30)
                             sink.os.write(rand1.data() + 30, 300);
                           else if (offset == 330)
                             sink.os.write(rand1.data() + 330, 670);
                           else if (offset == rand1.size())
                             sink.done();
                           return true;
                         },
                         "filename3",
                         {}});

    providers.push_back({"name4",
                         [&](size_t offset, httplib::DataSink &sink) -> bool {
                           // test the offset is given correctly at each step
                           if (!offset)
                             sink.os.write(rand2.data(), 2000);
                           else if (offset == 2000)
                             sink.os.write(rand2.data() + 2000, 1);
                           else if (offset == 2001)
                             sink.os.write(rand2.data() + 2001, 999);
                           else if (offset == rand2.size())
                             sink.done();
                           return true;
                         },
                         "filename4",
                         {}});

    {
      auto res = cli.Post("/post-providers", {}, {}, providers);
      ASSERT_TRUE(res);
      ASSERT_EQ(200, res->status);
    }

    {
      auto res = cli.Post("/post-both", {}, items, providers);
      ASSERT_TRUE(res);
      ASSERT_EQ(200, res->status);
    }
  }
}

TEST(MultipartFormDataTest, BadHeader) {
  Server svr;
  svr.Post("/post", [&](const Request & /*req*/, Response &res) {
    res.set_content("ok", "text/plain");
  });

  thread t = thread([&] { svr.listen(HOST, PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();

  const std::string body =
      "This is the preamble.  It is to be ignored, though it\r\n"
      "is a handy place for composition agents to include an\r\n"
      "explanatory note to non-MIME conformant readers.\r\n"
      "\r\n"
      "\r\n"
      "--simple boundary\r\n"
      "Content-Disposition: form-data; name=\"field1\"\r\n"
      ": BAD...\r\n"
      "\r\n"
      "value1\r\n"
      "--simple boundary\r\n"
      "Content-Disposition: form-data; name=\"field2\"; "
      "filename=\"example.txt\"\r\n"
      "\r\n"
      "value2\r\n"
      "--simple boundary--\r\n"
      "This is the epilogue.  It is also to be ignored.\r\n";

  std::string content_type =
      R"(multipart/form-data; boundary="simple boundary")";

  Client cli(HOST, PORT);
  auto res = cli.Post("/post", body, content_type.c_str());

  ASSERT_TRUE(res);
  EXPECT_EQ(400, res->status);
}

TEST(MultipartFormDataTest, WithPreamble) {
  Server svr;
  svr.Post("/post", [&](const Request & /*req*/, Response &res) {
    res.set_content("ok", "text/plain");
  });

  thread t = thread([&] { svr.listen(HOST, PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();

  const std::string body =
      "This is the preamble.  It is to be ignored, though it\r\n"
      "is a handy place for composition agents to include an\r\n"
      "explanatory note to non-MIME conformant readers.\r\n"
      "\r\n"
      "\r\n"
      "--simple boundary\r\n"
      "Content-Disposition: form-data; name=\"field1\"\r\n"
      "\r\n"
      "value1\r\n"
      "--simple boundary\r\n"
      "Content-Disposition: form-data; name=\"field2\"; "
      "filename=\"example.txt\"\r\n"
      "\r\n"
      "value2\r\n"
      "--simple boundary--\r\n"
      "This is the epilogue.  It is also to be ignored.\r\n";

  std::string content_type =
      R"(multipart/form-data; boundary="simple boundary")";

  Client cli(HOST, PORT);
  auto res = cli.Post("/post", body, content_type.c_str());

  ASSERT_TRUE(res);
  EXPECT_EQ(200, res->status);
}

TEST(MultipartFormDataTest, PostCustomBoundary) {
  SSLServer svr(SERVER_CERT_FILE, SERVER_PRIVATE_KEY_FILE);

  svr.Post("/post_customboundary", [&](const Request &req, Response & /*res*/,
                                       const ContentReader &content_reader) {
    if (req.is_multipart_form_data()) {
      MultipartFormDataItems files;
      content_reader(
          [&](const MultipartFormData &file) {
            files.push_back(file);
            return true;
          },
          [&](const char *data, size_t data_length) {
            files.back().content.append(data, data_length);
            return true;
          });

      EXPECT_TRUE(std::string(files[0].name) == "document");
      EXPECT_EQ(size_t(1024 * 1024 * 2), files[0].content.size());
      EXPECT_TRUE(files[0].filename == "2MB_data");
      EXPECT_TRUE(files[0].content_type == "application/octet-stream");

      EXPECT_TRUE(files[1].name == "hello");
      EXPECT_TRUE(files[1].content == "world");
      EXPECT_TRUE(files[1].filename == "");
      EXPECT_TRUE(files[1].content_type == "");
    } else {
      std::string body;
      content_reader([&](const char *data, size_t data_length) {
        body.append(data, data_length);
        return true;
      });
    }
  });

  auto t = std::thread([&]() { svr.listen("localhost", 8080); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();
  std::this_thread::sleep_for(std::chrono::seconds(1));

  {
    std::string data(1024 * 1024 * 2, '.');
    std::stringstream buffer;
    buffer << data;

    Client cli("https://localhost:8080");
    cli.enable_server_certificate_verification(false);

    MultipartFormDataItems items{
        {"document", buffer.str(), "2MB_data", "application/octet-stream"},
        {"hello", "world", "", ""},
    };

    auto res = cli.Post("/post_customboundary", {}, items, "abc-abc");
    ASSERT_TRUE(res);
    ASSERT_EQ(200, res->status);
  }
}

TEST(MultipartFormDataTest, PostInvalidBoundaryChars) {

  std::this_thread::sleep_for(std::chrono::seconds(1));

  std::string data(1024 * 1024 * 2, '&');
  std::stringstream buffer;
  buffer << data;

  Client cli("https://localhost:8080");

  MultipartFormDataItems items{
      {"document", buffer.str(), "2MB_data", "application/octet-stream"},
      {"hello", "world", "", ""},
  };

  for (const char &c : " \t\r\n") {
    auto res =
        cli.Post("/invalid_boundary", {}, items, string("abc123").append(1, c));
    ASSERT_EQ(Error::UnsupportedMultipartBoundaryChars, res.error());
    ASSERT_FALSE(res);
  }
}

TEST(MultipartFormDataTest, PutFormData) {
  SSLServer svr(SERVER_CERT_FILE, SERVER_PRIVATE_KEY_FILE);

  svr.Put("/put", [&](const Request &req, const Response & /*res*/,
                      const ContentReader &content_reader) {
    if (req.is_multipart_form_data()) {
      MultipartFormDataItems files;
      content_reader(
          [&](const MultipartFormData &file) {
            files.push_back(file);
            return true;
          },
          [&](const char *data, size_t data_length) {
            files.back().content.append(data, data_length);
            return true;
          });

      EXPECT_TRUE(std::string(files[0].name) == "document");
      EXPECT_EQ(size_t(1024 * 1024 * 2), files[0].content.size());
      EXPECT_TRUE(files[0].filename == "2MB_data");
      EXPECT_TRUE(files[0].content_type == "application/octet-stream");

      EXPECT_TRUE(files[1].name == "hello");
      EXPECT_TRUE(files[1].content == "world");
      EXPECT_TRUE(files[1].filename == "");
      EXPECT_TRUE(files[1].content_type == "");
    } else {
      std::string body;
      content_reader([&](const char *data, size_t data_length) {
        body.append(data, data_length);
        return true;
      });
    }
  });

  auto t = std::thread([&]() { svr.listen("localhost", 8080); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();
  std::this_thread::sleep_for(std::chrono::seconds(1));

  {
    std::string data(1024 * 1024 * 2, '&');
    std::stringstream buffer;
    buffer << data;

    Client cli("https://localhost:8080");
    cli.enable_server_certificate_verification(false);

    MultipartFormDataItems items{
        {"document", buffer.str(), "2MB_data", "application/octet-stream"},
        {"hello", "world", "", ""},
    };

    auto res = cli.Put("/put", items);
    ASSERT_TRUE(res);
    ASSERT_EQ(200, res->status);
  }
}

TEST(MultipartFormDataTest, PutFormDataCustomBoundary) {
  SSLServer svr(SERVER_CERT_FILE, SERVER_PRIVATE_KEY_FILE);

  svr.Put("/put_customboundary",
          [&](const Request &req, const Response & /*res*/,
              const ContentReader &content_reader) {
            if (req.is_multipart_form_data()) {
              MultipartFormDataItems files;
              content_reader(
                  [&](const MultipartFormData &file) {
                    files.push_back(file);
                    return true;
                  },
                  [&](const char *data, size_t data_length) {
                    files.back().content.append(data, data_length);
                    return true;
                  });

              EXPECT_TRUE(std::string(files[0].name) == "document");
              EXPECT_EQ(size_t(1024 * 1024 * 2), files[0].content.size());
              EXPECT_TRUE(files[0].filename == "2MB_data");
              EXPECT_TRUE(files[0].content_type == "application/octet-stream");

              EXPECT_TRUE(files[1].name == "hello");
              EXPECT_TRUE(files[1].content == "world");
              EXPECT_TRUE(files[1].filename == "");
              EXPECT_TRUE(files[1].content_type == "");
            } else {
              std::string body;
              content_reader([&](const char *data, size_t data_length) {
                body.append(data, data_length);
                return true;
              });
            }
          });

  auto t = std::thread([&]() { svr.listen("localhost", 8080); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();
  std::this_thread::sleep_for(std::chrono::seconds(1));

  {
    std::string data(1024 * 1024 * 2, '&');
    std::stringstream buffer;
    buffer << data;

    Client cli("https://localhost:8080");
    cli.enable_server_certificate_verification(false);

    MultipartFormDataItems items{
        {"document", buffer.str(), "2MB_data", "application/octet-stream"},
        {"hello", "world", "", ""},
    };

    auto res = cli.Put("/put_customboundary", {}, items, "abc-abc_");
    ASSERT_TRUE(res);
    ASSERT_EQ(200, res->status);
  }
}

TEST(MultipartFormDataTest, PutInvalidBoundaryChars) {

  std::this_thread::sleep_for(std::chrono::seconds(1));

  std::string data(1024 * 1024 * 2, '&');
  std::stringstream buffer;
  buffer << data;

  Client cli("https://localhost:8080");
  cli.enable_server_certificate_verification(false);

  MultipartFormDataItems items{
      {"document", buffer.str(), "2MB_data", "application/octet-stream"},
      {"hello", "world", "", ""},
  };

  for (const char &c : " \t\r\n") {
    auto res = cli.Put("/put", {}, items, string("abc123").append(1, c));
    ASSERT_EQ(Error::UnsupportedMultipartBoundaryChars, res.error());
    ASSERT_FALSE(res);
  }
}

#endif

#ifndef _WIN32
class UnixSocketTest : public ::testing::Test {
protected:
  void TearDown() override { std::remove(pathname_.c_str()); }

  void client_GET(const std::string &addr) {
    httplib::Client cli{addr};
    cli.set_address_family(AF_UNIX);
    ASSERT_TRUE(cli.is_valid());

    const auto &result = cli.Get(pattern_);
    ASSERT_TRUE(result) << "error: " << result.error();

    const auto &resp = result.value();
    EXPECT_EQ(resp.status, 200);
    EXPECT_EQ(resp.body, content_);
  }

  const std::string pathname_{"./httplib-server.sock"};
  const std::string pattern_{"/hi"};
  const std::string content_{"Hello World!"};
};

TEST_F(UnixSocketTest, pathname) {
  httplib::Server svr;
  svr.Get(pattern_, [&](const httplib::Request &, httplib::Response &res) {
    res.set_content(content_, "text/plain");
  });

  std::thread t{[&] {
    ASSERT_TRUE(svr.set_address_family(AF_UNIX).listen(pathname_, 80));
  }};
  auto se = detail::scope_exit([&] {
    svr.stop();
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();
  ASSERT_TRUE(svr.is_running());

  client_GET(pathname_);
}

#if defined(__linux__) ||                                                      \
    /* __APPLE__ */ (defined(SOL_LOCAL) && defined(SO_PEERPID))
TEST_F(UnixSocketTest, PeerPid) {
  httplib::Server svr;
  std::string remote_port_val;
  svr.Get(pattern_, [&](const httplib::Request &req, httplib::Response &res) {
    res.set_content(content_, "text/plain");
    remote_port_val = req.get_header_value("REMOTE_PORT");
  });

  std::thread t{[&] {
    ASSERT_TRUE(svr.set_address_family(AF_UNIX).listen(pathname_, 80));
  }};
  auto se = detail::scope_exit([&] {
    svr.stop();
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();
  ASSERT_TRUE(svr.is_running());

  client_GET(pathname_);
  EXPECT_EQ(std::to_string(getpid()), remote_port_val);
}
#endif

#ifdef __linux__
TEST_F(UnixSocketTest, abstract) {
  constexpr char svr_path[]{"\x00httplib-server.sock"};
  const std::string abstract_addr{svr_path, sizeof(svr_path) - 1};

  httplib::Server svr;
  svr.Get(pattern_, [&](const httplib::Request &, httplib::Response &res) {
    res.set_content(content_, "text/plain");
  });

  std::thread t{[&] {
    ASSERT_TRUE(svr.set_address_family(AF_UNIX).listen(abstract_addr, 80));
  }};
  auto se = detail::scope_exit([&] {
    svr.stop();
    t.join();
    ASSERT_FALSE(svr.is_running());
  });

  svr.wait_until_ready();
  ASSERT_TRUE(svr.is_running());

  client_GET(abstract_addr);
}
#endif

TEST(SocketStream, is_writable_UNIX) {
  int fds[2];
  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));

  const auto asSocketStream = [&](socket_t fd,
                                  std::function<bool(Stream &)> func) {
    return detail::process_client_socket(fd, 0, 0, 0, 0, func);
  };
  asSocketStream(fds[0], [&](Stream &s0) {
    EXPECT_EQ(s0.socket(), fds[0]);
    EXPECT_TRUE(s0.is_writable());

    EXPECT_EQ(0, close(fds[1]));
    EXPECT_FALSE(s0.is_writable());

    return true;
  });
  EXPECT_EQ(0, close(fds[0]));
}

TEST(SocketStream, is_writable_INET) {
  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(PORT + 1);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  int disconnected_svr_sock = -1;
  std::thread svr{[&] {
    const int s = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_LE(0, s);
    ASSERT_EQ(0, ::bind(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)));
    ASSERT_EQ(0, listen(s, 1));
    ASSERT_LE(0, disconnected_svr_sock = accept(s, nullptr, nullptr));
    ASSERT_EQ(0, close(s));
  }};
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::thread cli{[&] {
    const int s = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_LE(0, s);
    ASSERT_EQ(0, connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)));
    ASSERT_EQ(0, close(s));
  }};
  cli.join();
  svr.join();
  ASSERT_NE(disconnected_svr_sock, -1);

  const auto asSocketStream = [&](socket_t fd,
                                  std::function<bool(Stream &)> func) {
    return detail::process_client_socket(fd, 0, 0, 0, 0, func);
  };
  asSocketStream(disconnected_svr_sock, [&](Stream &ss) {
    EXPECT_EQ(ss.socket(), disconnected_svr_sock);
    EXPECT_FALSE(ss.is_writable());

    return true;
  });

  ASSERT_EQ(0, close(disconnected_svr_sock));
}
#endif // #ifndef _WIN32

TEST(TaskQueueTest, IncreaseAtomicInteger) {
  static constexpr unsigned int number_of_task{1000000};
  std::atomic_uint count{0};
  std::unique_ptr<TaskQueue> task_queue{
      new ThreadPool{CPPHTTPLIB_THREAD_POOL_COUNT}};

  for (unsigned int i = 0; i < number_of_task; ++i) {
    task_queue->enqueue(
        [&count] { count.fetch_add(1, std::memory_order_relaxed); });
  }

  EXPECT_NO_THROW(task_queue->shutdown());
  EXPECT_EQ(number_of_task, count.load());
}

TEST(RedirectTest, RedirectToUrlWithQueryParameters) {
  Server svr;

  svr.Get("/", [](const Request & /*req*/, Response &res) {
    res.set_redirect(R"(/hello?key=val%26key2%3Dval2)");
  });

  svr.Get("/hello", [](const Request &req, Response &res) {
    res.set_content(req.get_param_value("key"), "text/plain");
  });

  auto thread = std::thread([&]() { svr.listen(HOST, PORT); });
  auto se = detail::scope_exit([&] {
    svr.stop();
    thread.join();
    ASSERT_FALSE(svr.is_running());
  });

  std::this_thread::sleep_for(std::chrono::seconds(1));

  {
    Client cli(HOST, PORT);
    cli.set_follow_location(true);

    auto res = cli.Get("/");
    ASSERT_TRUE(res);
    EXPECT_EQ(200, res->status);
    EXPECT_EQ("val&key2=val2", res->body);
  }
}
