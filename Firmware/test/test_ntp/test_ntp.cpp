// ntpReplyPlausible(): NTP reply validation in timeSync (Tracker B1).
// ntpServerName(): which time server timeSync asks (configured, then DHCP option 42, then defaults).
#include <unity.h>
#include <string>
#include <string.h>
#include "ntpCheck.h"

static uint32_t ntpOf(uint64_t unixTime){ return (uint32_t)(unixTime + NTP_UNIX_OFFSET); }

static std::string pick(uint32_t i, const char* cfg, const uint32_t* dhcp, int n, uint8_t expectSource, size_t outLen = 80){
  char out[256]; memset(out, 'X', sizeof out);
  uint8_t src = ntpServerName(i, cfg, dhcp, n, out, outLen);
  TEST_ASSERT_EQUAL_UINT8(expectSource, src);
  TEST_ASSERT_TRUE(strlen(out) < outLen);                    // always terminated inside out
  TEST_ASSERT_EQUAL_CHAR('X', out[outLen]);                  // never writes past outLen
  return out;
}
static const uint32_t ip(int a, int b, int c, int d){ return a | b << 8 | c << 16 | (uint32_t)d << 24; }   // lwIP order

static void test_default_servers_rotate(void){
  const uint32_t none[3] = {0, 0, 0};
  TEST_ASSERT_EQUAL_STRING("time1.google.com", pick(0, nullptr, none, 3, ntpDefault).c_str());
  TEST_ASSERT_EQUAL_STRING("time4.google.com", pick(3, nullptr, none, 3, ntpDefault).c_str());
  TEST_ASSERT_EQUAL_STRING("time2.google.com", pick(5, "", nullptr, 0, ntpDefault).c_str());
  TEST_ASSERT_EQUAL_STRING("time1.google.com", pick(4, " ,; ", none, 3, ntpDefault).c_str());   // separators only
}

static void test_dhcp_servers(void){
  const uint32_t one[3] = {0, ip(192,0,2,1), 0};
  TEST_ASSERT_EQUAL_STRING("192.0.2.1", pick(0, nullptr, one, 3, ntpDHCP).c_str());
  TEST_ASSERT_EQUAL_STRING("192.0.2.1", pick(7, "", one, 3, ntpDHCP).c_str());
  const uint32_t two[3] = {ip(10,0,0,1), ip(255,255,255,254), 0};
  TEST_ASSERT_EQUAL_STRING("10.0.0.1", pick(0, nullptr, two, 3, ntpDHCP).c_str());
  TEST_ASSERT_EQUAL_STRING("255.255.255.254", pick(1, nullptr, two, 3, ntpDHCP).c_str());
  TEST_ASSERT_EQUAL_STRING("10.0.0.1", pick(2, nullptr, two, 3, ntpDHCP).c_str());
}

static void test_configured_servers_win(void){
  const uint32_t one[3] = {ip(192,0,2,1), 0, 0};
  TEST_ASSERT_EQUAL_STRING("ntp.lan", pick(0, "ntp.lan", one, 3, ntpConfigured).c_str());
  const char* list = " a.lan,b.lan ;\t10.1.2.3 ,";
  TEST_ASSERT_EQUAL_STRING("a.lan", pick(0, list, one, 3, ntpConfigured).c_str());
  TEST_ASSERT_EQUAL_STRING("b.lan", pick(1, list, one, 3, ntpConfigured).c_str());
  TEST_ASSERT_EQUAL_STRING("10.1.2.3", pick(2, list, one, 3, ntpConfigured).c_str());
  TEST_ASSERT_EQUAL_STRING("a.lan", pick(3, list, one, 3, ntpConfigured).c_str());
  TEST_ASSERT_EQUAL_STRING("a.lan", pick(0xFFFFFFFF, list, one, 3, ntpConfigured).c_str());   // 4294967295 % 3 == 0
}

// Names too long for the buffer are skipped; if none fit, fall back to the next source.
static void test_overlong_and_bad_names(void){
  const uint32_t one[3] = {ip(10,0,0,9), 0, 0};
  std::string longName(200, 'n');
  TEST_ASSERT_EQUAL_STRING("short.lan", pick(0, (longName + ",short.lan").c_str(), one, 3, ntpConfigured).c_str());
  TEST_ASSERT_EQUAL_STRING("10.0.0.9", pick(0, longName.c_str(), one, 3, ntpDHCP).c_str());
  TEST_ASSERT_EQUAL_STRING("time1.google.com", pick(0, longName.c_str(), nullptr, 0, ntpDefault).c_str());
  TEST_ASSERT_EQUAL_STRING("abcdefg", pick(0, "abcdefg", nullptr, 0, ntpConfigured, 8).c_str());   // exactly fits
  TEST_ASSERT_EQUAL_STRING("time1.g", pick(0, "abcdefgh", nullptr, 0, ntpDefault, 8).c_str());     // doesn't fit: truncated default
  for(uint32_t seed = 1; seed < 3000; seed++){                // fuzz: random bytes as the setting
    char cfg[64]; uint32_t s = seed; for(auto& c : cfg){ s = s * 1103515245 + 12345; c = (char)(s >> 16); }
    cfg[63] = 0;
    char out[33]; memset(out, 'X', sizeof out);
    ntpServerName(seed, cfg, one, 3, out, 32);
    TEST_ASSERT_TRUE(strlen(out) < 32); TEST_ASSERT_EQUAL_CHAR('X', out[32]);
  }
}


static void test_accepts_2018_through_2106(void){
  const uint64_t ok[] = {1527811200ULL,   // 2018-06-01
                         1795561084ULL,   // 2026-11-24
                         1830384000ULL,   // 2028-01-02 (rejected before the fix)
                         2208988800ULL,   // 2040-01-01, NTP era 1
                         4102444800ULL};  // 2100-01-01
  for(uint64_t t : ok){
    TEST_ASSERT_TRUE(ntpReplyPlausible(0x24, ntpOf(t)));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)t, ntpOf(t) - NTP_UNIX_OFFSET);   // uint32 math recovers UNIX time
  }
}
static void test_rejects_before_2018(void){
  TEST_ASSERT_FALSE(ntpReplyPlausible(0x24, ntpOf(1514678400ULL)));    // 2017-12-31
}
static void test_rejects_unsynchronized_server(void){
  TEST_ASSERT_FALSE(ntpReplyPlausible(0xE4, ntpOf(1795561084ULL)));    // leap indicator 3
}
static void test_rejects_zero_transmit_time(void){
  TEST_ASSERT_FALSE(ntpReplyPlausible(0x24, 0));
}

// Every flags value and a sweep of transmit times: only LI=3, zero and pre-2018 are rejected.
static void test_exhaustive_flags_and_sweep(void){
  for(int flags = 0; flags < 256; flags++){
    for(uint64_t t = 1400000000ULL; t < 4294967296ULL; t += 97000003ULL){
      bool expect = ((flags >> 6) != 3) && (t >= UNIX2018);
      TEST_ASSERT_EQUAL_INT(expect, ntpReplyPlausible((uint8_t)flags, ntpOf(t)));
    }
    TEST_ASSERT_FALSE(ntpReplyPlausible((uint8_t)flags, 0));
  }
}

// B72: a source whose servers never answer is left after NTP_FALLBACK_FAILURES failures in a row, in
// order configured -> DHCP -> defaults -> configured again; a success goes back to the preferred order.
// Simulates timeSync: each attempt asks ntpServerName(from), fails or not, then ntpFallback().
static void test_fallback_when_a_source_never_answers(void){
  const uint32_t dhcp[3] = {ip(192,168,1,1), 0, 0};                  // a router that hands itself out
  const char* cfg = "ntp.broken";
  uint8_t from = ntpConfigured, failures = 0;
  char out[80];
  auto attempt = [&](uint32_t i, bool ok){
    uint8_t used = ntpServerName(i, cfg, dhcp, 3, out, sizeof out, from);
    from = ntpFallback(from, used, failures, ok);
    return used;
  };
  for(int i = 0; i < NTP_FALLBACK_FAILURES; i++) TEST_ASSERT_EQUAL_UINT8(ntpConfigured, attempt(i, false));
  for(int i = 0; i < NTP_FALLBACK_FAILURES; i++){
    TEST_ASSERT_EQUAL_UINT8(ntpDHCP, attempt(10 + i, false));
    TEST_ASSERT_EQUAL_STRING("192.168.1.1", out);
  }
  TEST_ASSERT_EQUAL_UINT8(ntpDefault, attempt(20, false));             // the defaults: fail once, then succeed
  TEST_ASSERT_EQUAL_UINT8(ntpDefault, attempt(21, true));
  TEST_ASSERT_EQUAL_UINT8(ntpConfigured, from);                        // next sync prefers the configured again
  TEST_ASSERT_EQUAL_UINT8(0, failures);
  for(int i = 0; i < NTP_FALLBACK_FAILURES; i++) attempt(30 + i, false);   // everything failing: round and round
  for(int i = 0; i < NTP_FALLBACK_FAILURES; i++) attempt(40 + i, false);
  for(int i = 0; i < NTP_FALLBACK_FAILURES; i++) TEST_ASSERT_EQUAL_UINT8(ntpDefault, attempt(50 + i, false));
  TEST_ASSERT_EQUAL_UINT8(ntpConfigured, from);
}

// No configured servers: a failing DHCP server falls back straight to the defaults.
static void test_fallback_skips_empty_sources(void){
  const uint32_t dhcp[3] = {ip(10,0,0,1), 0, 0};
  uint8_t from = ntpConfigured, failures = 0; char out[80];
  for(int i = 0; i < NTP_FALLBACK_FAILURES; i++){
    uint8_t used = ntpServerName(i, nullptr, dhcp, 3, out, sizeof out, from);
    TEST_ASSERT_EQUAL_UINT8(ntpDHCP, used);
    from = ntpFallback(from, used, failures, false);
  }
  TEST_ASSERT_EQUAL_UINT8(ntpDefault, ntpServerName(9, nullptr, dhcp, 3, out, sizeof out, from));
  TEST_ASSERT_EQUAL_UINT8(ntpDefault, from);
}

int main(int, char**){
  UNITY_BEGIN();
  RUN_TEST(test_accepts_2018_through_2106);
  RUN_TEST(test_rejects_before_2018);
  RUN_TEST(test_rejects_unsynchronized_server);
  RUN_TEST(test_rejects_zero_transmit_time);
  RUN_TEST(test_exhaustive_flags_and_sweep);
  RUN_TEST(test_default_servers_rotate);
  RUN_TEST(test_dhcp_servers);
  RUN_TEST(test_configured_servers_win);
  RUN_TEST(test_overlong_and_bad_names);
  RUN_TEST(test_fallback_when_a_source_never_answers);
  RUN_TEST(test_fallback_skips_empty_sources);
  return UNITY_END();
}
