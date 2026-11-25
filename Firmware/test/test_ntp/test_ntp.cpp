// ntpReplyPlausible(): NTP reply validation in timeSync (Tracker B1).
#include <unity.h>
#include "ntpCheck.h"

static uint32_t ntpOf(uint64_t unixTime){ return (uint32_t)(unixTime + NTP_UNIX_OFFSET); }

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

int main(int, char**){
  UNITY_BEGIN();
  RUN_TEST(test_accepts_2018_through_2106);
  RUN_TEST(test_rejects_before_2018);
  RUN_TEST(test_rejects_unsynchronized_server);
  RUN_TEST(test_rejects_zero_transmit_time);
  RUN_TEST(test_exhaustive_flags_and_sweep);
  return UNITY_END();
}
