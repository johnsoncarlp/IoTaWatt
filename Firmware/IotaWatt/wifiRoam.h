#ifndef wifiRoam_h
#define wifiRoam_h

/*******************************************************************************************************
 * Wi-Fi roaming: move to a clearly stronger access point of the same network (Tracker F3)
 *
 * The ESP8266 reconnects to whichever access point of the SSID it finds and stays there however weak
 * it gets. While connected and the signal is weak, WiFiService scans in the background (at most every
 * WIFI_ROAM_SCAN_S) and moves to another BSSID of the same SSID that is at least WIFI_ROAM_GAIN_DB
 * stronger, at most once per WIFI_ROAM_HOLD_S. The move doesn't write flash, so the stored settings
 * (any access point) apply after a restart; if the chosen access point disappears, the connection is
 * reopened without a BSSID after WIFI_ROAM_UNLOCK_S, and that access point is skipped for WIFI_ROAM_AVOID_S
 * (one that refuses this device would otherwise cost an outage every WIFI_ROAM_HOLD_S). The "roam" device
 * setting (default on) turns it off.
 *
 * Everything runs in WiFiService (the loop): the scan is started asynchronously and its results are
 * polled with WiFi.scanComplete(). A scan-done callback would run in the SDK's system context, where
 * logging (SD writes) and WiFi.begin() aren't safe. A scan that never completes is given up after
 * WIFI_ROAM_SCAN_TIMEOUT_S, and the results are freed on every path.
 ******************************************************************************************************/

#include <stdint.h>
#include <string.h>

#define WIFI_ROAM_WEAK_DBM   -70        // consider roaming when weaker than this
#define WIFI_ROAM_GAIN_DB      8        // only for a candidate at least this much stronger
#define WIFI_ROAM_SCAN_S     300        // at most one scan per this many seconds
#define WIFI_ROAM_HOLD_S     600        // at most one move per this many seconds
#define WIFI_ROAM_UNLOCK_S    30        // disconnected this long after a move: reconnect to any AP
#define WIFI_ROAM_AVOID_S  21600        // then skip that access point this long (6 hours)
#define WIFI_ROAM_SCAN_TIMEOUT_S 30     // give up on a scan that hasn't completed after this long

struct roamCandidate {
  uint8_t bssid[6];
  int32_t rssi;
  int32_t channel;
  bool    sameSSID;
};

// Index of the access point to move to, or -1 to stay; never avoidBssid (an access point a move to
// failed recently). Pure (host tested).
inline int roamChoose(const uint8_t* currentBssid, int32_t currentRssi, const roamCandidate* list, int n,
                      const uint8_t* avoidBssid = nullptr, int32_t minGainDb = WIFI_ROAM_GAIN_DB){
  int best = -1;
  int32_t bestRssi = currentRssi + minGainDb - 1;
  for(int i = 0; list && i < n; i++){
    if( ! list[i].sameSSID) continue;
    if(currentBssid && memcmp(list[i].bssid, currentBssid, 6) == 0) continue;
    if(avoidBssid && memcmp(list[i].bssid, avoidBssid, 6) == 0) continue;
    if(list[i].rssi >= 0 || list[i].rssi < -100) continue;         // not a real reading
    if(list[i].rssi > bestRssi){
      best = i;
      bestRssi = list[i].rssi;
    }
  }
  return best;
}

// Should a scan start now? Pure (host tested).
inline bool roamShouldScan(bool enabled, int32_t rssi, uint32_t now, uint32_t lastScan, uint32_t lastMove){
  if( ! enabled) return false;
  if(rssi >= WIFI_ROAM_WEAK_DBM || rssi >= 0) return false;          // fine (or no reading)
  if(lastScan && now - lastScan < WIFI_ROAM_SCAN_S) return false;
  if(lastMove && now - lastMove < WIFI_ROAM_HOLD_S) return false;
  return true;
}

// Is a scan started at scanStart overdue (never completed)? Pure (host tested).
inline bool roamScanOverdue(uint32_t now, uint32_t scanStart){
  return now - scanStart >= WIFI_ROAM_SCAN_TIMEOUT_S;
}

// Is the access point a move failed to still to be skipped? Pure (host tested).
inline bool roamStillAvoided(uint32_t now, uint32_t failedAt){
  return failedAt && now - failedAt < WIFI_ROAM_AVOID_S;
}

#ifndef IOTAWATT_HOST_TEST
void wifiRoamConnected();               // call each WiFiService pass while connected
void wifiRoamDisconnected(uint32_t disconnectedSeconds);   // ... and while disconnected
#endif

#endif
