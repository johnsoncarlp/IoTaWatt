// JsonSummary / JsonDetail: split config.txt and tables.txt into a top-level summary plus condensed parts.
#include <unity.h>
#include "host_iotawatt.h"
#include "host_xbuf.h"
#include "jsonUtil.h"
#include "jsonSplit.cpp"

static File put(const std::string& text){
  SD.reset();
  auto d = SD.create("/c.txt");
  d->writeAt(0, (const uint8_t*)text.data(), text.size()); d->size = text.size();
  return SD.open("/c.txt", FILE_READ);
}
// Reference: remove whitespace outside strings.
static std::string condense(const std::string& s){
  std::string o; bool str = false, esc = false;
  for(char c : s){
    if(esc){ esc = false; o += c; continue; }
    if(str){ if(c == '\\') esc = true; else if(c == '"') str = false; o += c; continue; }
    if(isspace((unsigned char)c)) continue;
    if(c == '"') str = true;
    o += c;
  }
  return o;
}

static const char* CONFIG = R"({
  "format": 2,
  "timezone": "-5",
  "device": { "name": "IotaWatt", "channels": "15", "burden": [0, 20, 24] },
  "inputs": [ { "channel": 0, "type": "VT", "model": "TDC DA-10-09", "name": "Volts \"main\" {x}" },
              { "channel": 1, "type": "CT", "name": "a]b[c}d{" } ],
  "outputs": [],
  "update": "MAJOR"
})";

static void test_summary_and_details_round_trip(void){
  File f = put(CONFIG);
  String summary = JsonSummary(f, 1);
  JsonDocument d;
  TEST_ASSERT_FALSE_MESSAGE(deserializeJson(d, summary.c_str()), summary.c_str());
  TEST_ASSERT_EQUAL_INT(2, d["format"].as<int>());
  TEST_ASSERT_EQUAL_STRING("MAJOR", d["update"].as<const char*>());
  for(const char* key : {"device", "inputs", "outputs"}){
    JsonArray loc = d[key];
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, loc.size(), key);
    char* part = JsonDetail(f, loc);
    // expected: the condensed text of that value in the original
    std::string text = CONFIG; size_t at = text.find(std::string("\"") + key + "\"");
    size_t beg = text.find_first_of("[{", at), depth = 0, end = beg; bool str = false, esc = false;
    for(; end < text.size(); end++){
      char c = text[end];
      if(esc){ esc = false; continue; } if(str){ if(c == '\\') esc = true; else if(c == '"') str = false; continue; }
      if(c == '"') str = true; else if(c == '[' || c == '{') depth++; else if(c == ']' || c == '}'){ if(--depth == 0) break; }
    }
    TEST_ASSERT_EQUAL_STRING_MESSAGE(condense(text.substr(beg, end - beg + 1)).c_str(), part, key);
    JsonDocument p; TEST_ASSERT_FALSE_MESSAGE(deserializeJson(p, part), key);
    delete[] part;
  }
}

static void test_depth_two(void){
  File f = put(CONFIG);
  String summary = JsonSummary(f, 2);
  JsonDocument d;
  TEST_ASSERT_FALSE_MESSAGE(deserializeJson(d, summary.c_str()), summary.c_str());
  TEST_ASSERT_EQUAL_STRING("IotaWatt", d["device"]["name"].as<const char*>());
  JsonArray burden = d["device"]["burden"];
  char* part = JsonDetail(f, burden);
  TEST_ASSERT_EQUAL_STRING("[0,20,24]", part);
  delete[] part;
}

// ---- bad input: must not crash or read/write out of bounds ----

static void bad(const std::string& text, int depth){
  File f = put(text);
  String s = JsonSummary(f, depth);
  JsonDocument d;
  if(!deserializeJson(d, s.c_str()) && d.is<JsonObject>()){
    for(JsonPair kv : d.as<JsonObject>()){
      JsonArray loc = kv.value().as<JsonArray>();
      if(loc.size() == 2 && loc[0].is<int>() && loc[1].is<int>() && loc[1].as<int>() >= 0 && loc[1].as<int>() < 100000){
        delete[] JsonDetail(f, loc);
      }
    }
  }
}

static void test_bad_inputs(void){
  bad("", 1);
  bad("   \n\t ", 1);
  bad("]", 1);
  bad("}{", 1);
  bad("{\"a\":[1,2", 1);
  bad("{\"a\":\"unterminated", 1);
  bad("{\"a\":\"\\", 1);
  bad("{\"a\":{\"b\":{\"c\":1}}}}}}}", 1);
  bad("[[[[[]]]]]", 2);
}

static void test_deep_nesting(void){
  // JsonSummary tracks nesting in a fixed array; a corrupted config can nest arbitrarily deep.
  std::string deep = "{\"a\":" + std::string(40, '[') + std::string(40, ']') + "}";
  bad(deep, 1);
  std::string deeper = std::string(200, '{');
  bad(deeper, 1);
}

static void test_fuzz_random_json(void){
  uint32_t seed = 2468;
  auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
  const char alphabet[] = "{}[]\"\\:,ab1 \n";
  for(int iter = 0; iter < 20000; iter++){
    std::string s; int len = rnd() % 60;
    for(int i = 0; i < len; i++) s += alphabet[rnd() % (sizeof(alphabet) - 1)];
    bad(s, 1 + rnd() % 2);
  }
}

int main(int, char**){
  UNITY_BEGIN();
  RUN_TEST(test_summary_and_details_round_trip);
  RUN_TEST(test_depth_two);
  RUN_TEST(test_bad_inputs);
  RUN_TEST(test_deep_nesting);
  RUN_TEST(test_fuzz_random_json);
  return UNITY_END();
}
