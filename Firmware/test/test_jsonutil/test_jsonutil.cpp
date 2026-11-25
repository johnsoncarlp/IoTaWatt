// jsonUtil.h: the ArduinoJson 5 compatible helpers used wherever user configuration is read.
#include <unity.h>
#include <string>
#include "jsonUtil.h"

struct NumCase { const char* json; long i; unsigned long u; float f; int b; bool deliberate; };
struct StrCase { const char* json; const char* text; };
#include "v5_golden.inc"

static JsonDocument doc;
static JsonVariantConst parseValue(const char* json){
  std::string j = strcmp(json, "<missing>") ? std::string("{\"v\":") + json + "}" : "{}";
  doc.clear();
  TEST_ASSERT_FALSE_MESSAGE(deserializeJson(doc, j), j.c_str());
  return doc["v"];
}

static void test_numbers_match_v5(void){
  char msg[96];
  for(const NumCase& c : NUM_CASES){
    JsonVariantConst v = parseValue(c.json);
    snprintf(msg, sizeof msg, "value %s%s", c.json, c.deliberate ? " (deliberate v5 difference)" : "");
    TEST_ASSERT_EQUAL_INT32_MESSAGE((int32_t)c.i, jInt<int>(v), msg);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)c.u, jInt<unsigned int>(v), msg);
    float f = jFloat(v);
    if(std::isnan(c.f)) TEST_ASSERT_TRUE_MESSAGE(std::isnan(f), msg);
    else if(std::isinf(c.f)) TEST_ASSERT_EQUAL_FLOAT_MESSAGE(c.f, f, msg);
    else TEST_ASSERT_FLOAT_WITHIN_MESSAGE(fabsf(c.f) * 1e-6f + 1e-9f, c.f, f, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(c.b, (int)jBool(v), msg);
  }
}

static void test_strings_match_v5(void){
  for(const StrCase& c : STR_CASES){
    JsonVariantConst v = parseValue(c.json);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(c.text, jStrAs<std::string>(v).c_str(), c.json);
  }
}

static void test_long_numeric_string(void){
  // More than the 39 characters jFloat buffers: falls back to strtof on the whole text.
  JsonVariantConst v = parseValue("\"1.0000000000000000000000000000000000000001e2\"");
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 100.0f, jFloat(v));
}

static void test_web_ui_quoted_values(void){
  // The web app writes quoted numbers ("channels":"15"). ArduinoJson 7 converts well-formed ones too;
  // the helpers matter for the looser forms v5 accepted and for numbers read as text.
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, "{\"channels\":\"15\",\"stop\":\"false\",\"systemid\":12345}"));
  TEST_ASSERT_EQUAL_UINT32(15u, d["channels"].as<unsigned int>());   // well-formed: same in plain v7
  TEST_ASSERT_EQUAL_UINT32(15u, jInt<unsigned int>(d["channels"]));
  TEST_ASSERT_TRUE(d["stop"].as<bool>());                  // plain v7: any non-empty string is true...
  TEST_ASSERT_FALSE(jBool(d["stop"]));                     // ...v5 behaviour: "false" is false
  TEST_ASSERT_NULL(d["systemid"].as<const char*>());       // plain v7: a number read as text is null
  TEST_ASSERT_EQUAL_STRING("12345", jStrAs<std::string>(d["systemid"]).c_str());
}

static void test_config_comments_accepted(void){
  // ArduinoJson 5 skipped comments in config.txt; the build enables ARDUINOJSON_ENABLE_COMMENTS.
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, "/* note */ {\"v\":\"x\" // trailing\n}"));
  TEST_ASSERT_EQUAL_STRING("x", d["v"].as<const char*>());
}

// Bad values a hand-edited config can contain. None may crash or trip the sanitizers; the results
// are pinned where they matter (helpers return 0/false/"" rather than garbage).
static void test_bad_values(void){
  struct { const char* json; long i; float f; int b; const char* text; } bad[] = {
    {"\"99999999999999999999\"",  1661992959,    1e20f, 1, "99999999999999999999"},  // overflow wraps as in v5
    {"\"-\"",                      0,            0.0f,  0, "-"},
    {"\"+\"",                      0,            0.0f,  0, "+"},
    {"\"e5\"",                     0,            0.0f,  0, "e5"},
    {"\"1e\"",                     1,            1.0f,  1, "1e"},
    {"\"\\u0000\"",              0,            0.0f,  0, ""},                      // v7 decodes to NUL; v5 gave "u0000"
    {"\"\\r\\n15\"",           0,            0.0f,  0, "\r\n15"},
    {"1e400",                        0,            INFINITY, 1, "1e400"},             // beyond double range
    {"-1e400",                       0,            -INFINITY, 1, "-1e400"},
  };
  char msg[64];
  for(auto& c : bad){
    JsonVariantConst v = parseValue(c.json);
    snprintf(msg, sizeof msg, "bad value %s", c.json);
    (void)jInt<int>(v); (void)jInt<unsigned int>(v); (void)jFloat(v); (void)jBool(v);  // must not crash
    if(v.is<const char*>()){
      TEST_ASSERT_EQUAL_INT32_MESSAGE((int32_t)c.i, jInt<int>(v), msg);
      TEST_ASSERT_EQUAL_INT_MESSAGE(c.b, (int)jBool(v), msg);
      TEST_ASSERT_EQUAL_STRING_MESSAGE(c.text, jStrAs<std::string>(v).c_str(), msg);
    }
  }
  // Wrong container types never crash and read as empty.
  for(const char* j : {"[]", "{}", "[[[[1]]]]", "{\"a\":{\"b\":[1,2,{}]}}"}){
    JsonVariantConst v = parseValue(j);
    TEST_ASSERT_EQUAL_INT32(0, jInt<int>(v));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, jFloat(v));
    TEST_ASSERT_FALSE(jBool(v));
    TEST_ASSERT_EQUAL_STRING("", jStrAs<std::string>(v).c_str());
  }
}

// Random strings through every helper: must never crash or read out of bounds.
static void test_fuzz_random_strings(void){
  uint32_t seed = 777;
  auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
  const char alphabet[] = "0123456789+-.eEtTnNiIaf x\t";
  for(int iter = 0; iter < 20000; iter++){
    char s[80]; int len = rnd() % 75;
    for(int i = 0; i < len; i++) s[i] = alphabet[rnd() % (sizeof(alphabet) - 1)];
    s[len] = 0;
    JsonDocument d; d["v"] = s;
    JsonVariantConst v = d["v"];
    (void)jInt<int>(v); (void)jInt<unsigned int>(v); (void)jInt<long long>(v);
    (void)jFloat(v); (void)jBool(v);
    TEST_ASSERT_EQUAL_STRING(s, jStrAs<std::string>(v).c_str());
  }
}

int main(int, char**){
  UNITY_BEGIN();
  RUN_TEST(test_numbers_match_v5);
  RUN_TEST(test_strings_match_v5);
  RUN_TEST(test_long_numeric_string);
  RUN_TEST(test_web_ui_quoted_values);
  RUN_TEST(test_config_comments_accepted);
  RUN_TEST(test_bad_values);
  RUN_TEST(test_fuzz_random_strings);
  return UNITY_END();
}
