// UTC2Local / local2UTC / testRule with the web UI's four DST presets, checked against the IANA tz database.
#include <unity.h>
#include "host_iotawatt.h"
#include "timeServices.h"
int32_t localTimeDiff = 0;
tzRule* timezoneRule = nullptr;
#include "timeRules.cpp"
#include "tz_oracle.inc"

static tzRule preset(int which){
  // SD/index.htm dstRules[]: Americas, Europe, Australia, NewZealand
  struct P { int adj; bool utc; int bm, bw, bi, bt, em, ew, ei, et; } p[] = {
    {60, false,  3, 1,  2, 120, 11, 1,  1, 120},
    {60, true,   3, 1, -1,  60, 10, 1, -1,  60},
    {60, false, 10, 1,  1, 120,  4, 1,  1, 180},
    {60, false,  9, 1, -1, 120,  4, 1,  1, 180}};
  tzRule r; P& x = p[which];
  r.adjMinutes = x.adj; r.useUTC = x.utc;
  r.begPeriod.month = x.bm; r.begPeriod.weekday = x.bw; r.begPeriod.instance = x.bi; r.begPeriod.time = x.bt;
  r.endPeriod.month = x.em; r.endPeriod.weekday = x.ew; r.endPeriod.instance = x.ei; r.endPeriod.time = x.et;
  return r;
}

void setUp(void){ timezoneRule = nullptr; localTimeDiff = 0; }
void tearDown(void){}

static void test_presets_match_tz_database(void){
  int totalBad = 0;
  for(const TzZone& z : TZ_ZONES){
    tzRule r = preset(z.rule);
    timezoneRule = &r; localTimeDiff = (int32_t)(z.tzHours * 60);
    int bad = 0;
    for(int i = z.first; i < z.first + z.count; i++){
      uint32_t utc = TZ_ROWS[i][0], want = TZ_ROWS[i][1], got = UTC2Local(utc);
      if(got != want){
        if(bad < 3){ time_t t = utc; char b[32]; strftime(b, sizeof b, "%Y-%m-%d %H:%M", gmtime(&t));
          printf("  %s: UTC %s -> local off by %+d min\n", z.name, b, (int)((int64_t)got - (int64_t)want) / 60); }
        bad++;
      }
    }
    if(bad) printf("%s: %d of %d samples wrong\n", z.name, bad, z.count);
    totalBad += bad;
  }
  TEST_ASSERT_EQUAL_INT(0, totalBad);
}

static void test_local2UTC_round_trip_away_from_transitions(void){
  for(const TzZone& z : TZ_ZONES){
    tzRule r = preset(z.rule);
    timezoneRule = &r; localTimeDiff = (int32_t)(z.tzHours * 60);
    for(int i = z.first; i < z.first + z.count; i++){
      uint32_t u = TZ_ROWS[i][0];
      int64_t o1 = (int64_t)UTC2Local(u - 7200) - (u - 7200), o2 = (int64_t)UTC2Local(u + 7200) - (u + 7200);
      if(o1 != o2) continue;                               // within 2 h of a change: ambiguous/skipped hour
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(u, local2UTC(UTC2Local(u)), z.name);
    }
  }
}

static void test_no_rule_is_plain_offset(void){
  localTimeDiff = -330;                                    // e.g. -5:30
  TEST_ASSERT_EQUAL_UINT32(1795561084u - 330 * 60, UTC2Local(1795561084u));
  TEST_ASSERT_EQUAL_UINT32(1795561084u, local2UTC(1795561084u - 330 * 60));
}

// A "last Sunday of February" rule: in 2032 Feb 29 is a Sunday, so the period must start on the 29th.
static void test_last_weekday_of_february_in_leap_year(void){
  dateTimeRule last; last.month = 2; last.weekday = 1; last.instance = -1; last.time = 0;
  struct tm tm{}; tm.tm_year = 2032 - 1900; tm.tm_mon = 1; tm.tm_mday = 28;
  uint32_t feb28_2032 = (uint32_t)timegm(&tm);             // 2032-02-28 00:00 UTC (a Saturday)
  TEST_ASSERT_EQUAL_UINT32(1961539200u, feb28_2032);
  uint32_t feb29_2032 = feb28_2032 + 86400;                // Sunday
  TEST_ASSERT_FALSE(testRule(feb28_2032 + 43200, last));
  TEST_ASSERT_TRUE(testRule(feb29_2032 + 60, last));
  uint32_t feb22_2032 = feb28_2032 - 6 * 86400;            // the previous Sunday
  TEST_ASSERT_FALSE(testRule(feb22_2032 + 60, last));
}

// Bad rules from a hand-edited config must not crash or hit UB (sanitizers).
static void test_fuzz_bad_rules(void){
  uint32_t seed = 31337;
  auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
  for(int iter = 0; iter < 20000; iter++){
    tzRule r;
    r.begPeriod.month = (int8_t)(rnd() % 40 - 13); r.begPeriod.weekday = (int8_t)(rnd() % 20 - 5);
    r.begPeriod.instance = (int8_t)(rnd() % 20 - 10); r.begPeriod.time = (int16_t)(rnd() % 4000 - 1000);
    r.endPeriod.month = (int8_t)(rnd() % 40 - 13); r.endPeriod.weekday = (int8_t)(rnd() % 20 - 5);
    r.endPeriod.instance = (int8_t)(rnd() % 20 - 10); r.endPeriod.time = (int16_t)(rnd() % 4000 - 1000);
    r.adjMinutes = (int16_t)(rnd() % 400 - 200); r.useUTC = rnd() % 2;
    timezoneRule = &r; localTimeDiff = (int32_t)(rnd() % 1500) - 750;
    uint32_t u = 1514796044u + (uint32_t)(((uint64_t)rnd() << 15 | rnd()) * 37 % 1000000000u);
    uint32_t l = UTC2Local(u);
    (void)local2UTC(l);
    TEST_ASSERT_TRUE(llabs((int64_t)l - u) <= 750LL * 60 + 400 * 60 * 2);   // never more than offset + 2 adjustments
  }
}

int main(int, char**){
  UNITY_BEGIN();
  RUN_TEST(test_presets_match_tz_database);
  RUN_TEST(test_local2UTC_round_trip_away_from_transitions);
  RUN_TEST(test_no_rule_is_plain_offset);
  RUN_TEST(test_last_weekday_of_february_in_leap_year);
  RUN_TEST(test_fuzz_bad_rules);
  return UNITY_END();
}
