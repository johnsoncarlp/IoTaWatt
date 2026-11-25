// Wi-Fi roaming decisions (wifiRoam.h): when to scan, and which access point to move to. Tracker F3.
#include <unity.h>
#include "wifiRoam.h"

static roamCandidate ap(uint8_t id, int32_t rssi, bool same = true, int32_t ch = 1){
  roamCandidate c{}; memset(c.bssid, 0, 6); c.bssid[5] = id; c.rssi = rssi; c.channel = ch; c.sameSSID = same; return c;
}
static const uint8_t CUR[6] = {0, 0, 0, 0, 0, 1};

void setUp(void){}
void tearDown(void){}

static void test_choose_needs_clear_gain(void){
  roamCandidate l[] = {ap(1, -80), ap(2, -73)};                  // current (-80) and one 7 dB stronger
  TEST_ASSERT_EQUAL_INT(-1, roamChoose(CUR, -80, l, 2));
  l[1].rssi = -72;                                               // 8 dB: move
  TEST_ASSERT_EQUAL_INT(1, roamChoose(CUR, -80, l, 2));
}

static void test_choose_strongest_of_same_network(void){
  roamCandidate l[] = {ap(2, -60, false), ap(3, -70), ap(1, -82), ap(4, -61), ap(5, -55, false)};
  TEST_ASSERT_EQUAL_INT(3, roamChoose(CUR, -82, l, 5));          // -61; not the stronger other networks
}

static void test_choose_ignores_current_and_bad_readings(void){
  roamCandidate l[] = {ap(1, -40), ap(2, 0), ap(3, 31), ap(4, -127)};
  TEST_ASSERT_EQUAL_INT(-1, roamChoose(CUR, -85, l, 4));          // own BSSID; 0/positive/absurd RSSI
  TEST_ASSERT_EQUAL_INT(-1, roamChoose(CUR, -85, nullptr, 3));
  TEST_ASSERT_EQUAL_INT(-1, roamChoose(CUR, -85, l, 0));
  roamCandidate one[] = {ap(1, -60)};
  TEST_ASSERT_EQUAL_INT(0, roamChoose(nullptr, -85, one, 1));    // no current BSSID: anything clearly stronger
}

static void test_should_scan(void){
  const uint32_t now = 1000000;
  TEST_ASSERT_FALSE(roamShouldScan(false, -85, now, 0, 0));      // disabled
  TEST_ASSERT_FALSE(roamShouldScan(true, -70, now, 0, 0));       // not weak (threshold is "weaker than -70")
  TEST_ASSERT_TRUE(roamShouldScan(true, -71, now, 0, 0));
  TEST_ASSERT_FALSE(roamShouldScan(true, 0, now, 0, 0));         // no reading
  TEST_ASSERT_FALSE(roamShouldScan(true, -85, now, now - WIFI_ROAM_SCAN_S + 1, 0));   // scanned recently
  TEST_ASSERT_TRUE(roamShouldScan(true, -85, now, now - WIFI_ROAM_SCAN_S, 0));
  TEST_ASSERT_FALSE(roamShouldScan(true, -85, now, 0, now - WIFI_ROAM_HOLD_S + 1));   // moved recently
  TEST_ASSERT_TRUE(roamShouldScan(true, -85, now, 0, now - WIFI_ROAM_HOLD_S));
}

// 24Nov2026 on Carl's device: stuck at -79..-82 dBm on channel 6 with channel 1 at -61..-65.
static void test_carls_case(void){
  roamCandidate l[] = {ap(1, -81, true, 6), ap(2, -63, true, 1)};
  TEST_ASSERT_TRUE(roamShouldScan(true, -81, 5000, 0, 0));
  TEST_ASSERT_EQUAL_INT(1, roamChoose(CUR, -81, l, 2));
  TEST_ASSERT_EQUAL_INT(-1, roamChoose(l[1].bssid, -63, l, 2));   // and then stays (no move back)
  TEST_ASSERT_FALSE(roamShouldScan(true, -63, 5100, 5000, 5000));
}

// B69: an access point a move to failed is skipped, even when clearly the strongest.
static void test_choose_skips_avoided(void){
  roamCandidate l[] = {ap(1, -82), ap(2, -55), ap(3, -66)};
  TEST_ASSERT_EQUAL_INT(1, roamChoose(CUR, -82, l, 3));
  TEST_ASSERT_EQUAL_INT(2, roamChoose(CUR, -82, l, 3, l[1].bssid));     // the next best instead
  roamCandidate only[] = {ap(1, -82), ap(2, -55)};
  TEST_ASSERT_EQUAL_INT(-1, roamChoose(CUR, -82, only, 2, only[1].bssid));
}

static void test_avoid_and_scan_timeouts(void){
  const uint32_t t = 2000000;
  TEST_ASSERT_FALSE(roamStillAvoided(t, 0));                             // nothing failed
  TEST_ASSERT_TRUE(roamStillAvoided(t, t));
  TEST_ASSERT_TRUE(roamStillAvoided(t + WIFI_ROAM_AVOID_S - 1, t));
  TEST_ASSERT_FALSE(roamStillAvoided(t + WIFI_ROAM_AVOID_S, t));
  TEST_ASSERT_FALSE(roamScanOverdue(t + WIFI_ROAM_SCAN_TIMEOUT_S - 1, t)); // B70: a scan that never ends
  TEST_ASSERT_TRUE(roamScanOverdue(t + WIFI_ROAM_SCAN_TIMEOUT_S, t));
}

int main(int, char**){
  UNITY_BEGIN();
  RUN_TEST(test_choose_needs_clear_gain);
  RUN_TEST(test_choose_strongest_of_same_network);
  RUN_TEST(test_choose_ignores_current_and_bad_readings);
  RUN_TEST(test_should_scan);
  RUN_TEST(test_carls_case);
  RUN_TEST(test_choose_skips_avoided);
  RUN_TEST(test_avoid_and_scan_timeouts);
  return UNITY_END();
}
