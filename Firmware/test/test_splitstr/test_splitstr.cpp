// splitstr: used by the influxDB2 uploader to parse query responses (CSV with CRLF line ends).
#include <unity.h>
#include "splitstr.cpp"

static void check(const char* in, char sep, char end, int n, const char* const* expect){
  splitstr s(in, sep, end);
  TEST_ASSERT_EQUAL_INT_MESSAGE(n, s.length(), in);
  for(int i = 0; i < n; i++) TEST_ASSERT_EQUAL_STRING_MESSAGE(expect[i], s[i], in);
}

static void test_trims_spaces(void){
  const char* e[] = {"first element", "abc", "third element"};
  check("first element,  abc  , third element    ", ',', 0, 3, e);
}
static void test_empty_entries(void){
  const char* e1[] = {"", "x"};        check(",x", ',', 0, 2, e1);
  const char* e2[] = {""};             check("   ", ',', 0, 1, e2);
  const char* e3[] = {""};             check("", ',', 0, 1, e3);
  const char* e4[] = {"", "", ""};     check(" , ,", ',', 0, 3, e4);
}
static void test_influx_csv_header(void){
  // Influx ends lines with CRLF: the last field keeps its CR (the uploader matches "_time" and "_time\r").
  const char* e[] = {"", "result", "table", "_measurement", "_time\r"};
  check(",result,table,_measurement,_time\r\n,_result,0,Total,1795561080\r\n", ',', '\n', 5, e);
}
static void test_missing_end_char(void){
  splitstr s("no newline here", ',', '\n');
  TEST_ASSERT_EQUAL_INT(0, s.length());
}

// Bad input: random bytes, separators and end chars in any position. Under AddressSanitizer any
// out-of-bounds access fails the run; every entry must be a terminated string inside the copy.
static void test_fuzz_random_lines(void){
  uint32_t seed = 12345;
  auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
  const char alphabet[] = "  ,,,\r\n\nab_09\t;";
  char line[64];
  for(int iter = 0; iter < 20000; iter++){
    int len = rnd() % 60;
    for(int i = 0; i < len; i++) line[i] = (rnd() % 8) ? alphabet[rnd() % (sizeof(alphabet) - 1)] : (char)(1 + rnd() % 255);
    line[len] = 0;
    char end = (rnd() % 2) ? '\n' : 0;
    splitstr s(line, ',', end);
    for(int i = 0; i < s.length(); i++){
      TEST_ASSERT_NOT_NULL(s[i]);
      TEST_ASSERT_TRUE(strlen(s[i]) <= (size_t)len);
    }
  }
}

int main(int, char**){
  UNITY_BEGIN();
  RUN_TEST(test_trims_spaces);
  RUN_TEST(test_empty_entries);
  RUN_TEST(test_influx_csv_header);
  RUN_TEST(test_missing_end_char);
  RUN_TEST(test_fuzz_random_lines);
  return UNITY_END();
}
