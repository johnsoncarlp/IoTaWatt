// xurl: the URL splitter the uploaders use for user-entered server URLs (parse, setters, build).
#include <unity.h>
#include "Arduino.h"
#include "xurl.cpp"

static std::string s(const char* p){ return p ? p : "(null)"; }

void setUp(void){}
void tearDown(void){}

static void test_parse_parts(void){
  struct { const char* url; const char *method, *auth, *domain, *port, *path, *query; } c[] = {
    {"http://influx.lan:8086/write?db=x", "http://", nullptr, "influx.lan", ":8086", "/write", "?db=x"},
    {"https://user:pw@emoncms.org/input", "https://", "user:pw@", "emoncms.org", nullptr, "/input", nullptr},
    {"10.0.0.5", "http://", nullptr, "10.0.0.5", nullptr, nullptr, nullptr},
    {"host/", "http://", nullptr, "host", nullptr, nullptr, nullptr},
    {"host/a/b/", "http://", nullptr, "host", nullptr, "/a/b", nullptr},
    {"host?q=1", "http://", nullptr, "host", nullptr, nullptr, "?q=1"},
  };
  for(auto& k : c){
    xurl u; TEST_ASSERT_TRUE_MESSAGE(u.parse(k.url), k.url);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(s(k.method).c_str(), s(u.method()).c_str(), k.url);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(s(k.auth).c_str(),   s(u.auth()).c_str(), k.url);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(s(k.domain).c_str(), s(u.domain()).c_str(), k.url);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(s(k.port).c_str(),   s(u.port()).c_str(), k.url);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(s(k.path).c_str(),   s(u.path()).c_str(), k.url);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(s(k.query).c_str(),  s(u.query()).c_str(), k.url);
  }
}

static void test_rejects_bad_urls(void){
  const char* bad[] = {"", "http://", "http://:8086", "http://host:", "http://host:/x", "://", "@", "http://@", nullptr};
  for(int i = 0; bad[i]; i++){ xurl u; TEST_ASSERT_FALSE_MESSAGE(u.parse(bad[i]), bad[i]); }
  xurl u; TEST_ASSERT_FALSE(u.parse(nullptr));
}

static void test_build_round_trip_and_setters(void){
  xurl u; u.parse("http://influx.lan:8086/write?db=x");
  TEST_ASSERT_EQUAL_STRING("http://influx.lan:8086/write?db=x", u.build().c_str());
  u.path("/query"); u.query("?q=SELECT"); u.port(nullptr);
  TEST_ASSERT_EQUAL_STRING("http://influx.lan/query?q=SELECT", u.build().c_str());
  u.parse("https://a.b");                                     // reparse frees and resets every part
  TEST_ASSERT_EQUAL_STRING("https://a.b", u.build().c_str());
}

// Random bytes and mutated URLs: no crash, overread or leak (ASan); a successful parse always has a
// method and a domain, and build() is a string made of the parts.
static void test_fuzz(void){
  const char* seeds[] = {"http://u:p@h.lan:8086/a/b?c=d", "https://x/", "h:1/?", "a@b:2"};
  uint32_t r = 7;
  for(int n = 0; n < 20000; n++){
    char buf[48]; const char* seed = seeds[n % 4]; size_t len = strlen(seed); memcpy(buf, seed, len + 1);
    for(int m = 0; m < 1 + n % 4; m++){ r = r * 1103515245 + 12345; size_t at = (r >> 8) % (len + 1);
      r = r * 1103515245 + 12345; char ch = "/:@?.x9\0"[(r >> 8) % 8];
      if(at < len) buf[at] = ch; }
    if(n % 5 == 0){ for(size_t i = 0; i < sizeof buf - 1; i++){ r = r * 1103515245 + 12345; buf[i] = (char)(r >> 16); } buf[sizeof buf - 1] = 0; }
    xurl u;
    if(u.parse(buf)){
      TEST_ASSERT_NOT_NULL(u.method()); TEST_ASSERT_NOT_NULL(u.domain());
      TEST_ASSERT_TRUE(u.build().length() >= strlen(u.domain()));
    }
  }
}

int main(int, char**){
  UNITY_BEGIN();
  RUN_TEST(test_parse_parts);
  RUN_TEST(test_rejects_bad_urls);
  RUN_TEST(test_build_round_trip_and_setters);
  RUN_TEST(test_fuzz);
  return UNITY_END();
}
