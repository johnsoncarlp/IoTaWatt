#include "IotaWatt.h"
#include "wifiRoam.h"

static uint32_t roamLastScan = 0;
static uint32_t roamLastMove = 0;
static bool     roamScanning = false;
static bool     roamLocked = false;     // connected with a BSSID set by a move
static uint8_t  roamTarget[6];          // the access point of the last move
static uint8_t  roamAvoid[6];           // an access point a move to failed, skipped until ...
static uint32_t roamAvoidSince = 0;     // ... WIFI_ROAM_AVOID_S after this

        // The scan has results: choose, and maybe move. Called from WiFiService (loop context).

static void roamScanDone(int found){
  if(found <= 0) return;
  String ssid = WiFi.SSID();
  int32_t currentRssi = WiFi.RSSI();
  uint8_t current[6];
  memcpy(current, WiFi.BSSID(), 6);
  int n = found < 16 ? found : 16;
  roamCandidate list[16];
  for(int i = 0; i < n; i++){
    memcpy(list[i].bssid, WiFi.BSSID(i), 6);
    list[i].rssi = WiFi.RSSI(i);
    list[i].channel = WiFi.channel(i);
    list[i].sameSSID = WiFi.SSID(i) == ssid;
  }
  const uint8_t* avoid = roamStillAvoided(UTCtime(), roamAvoidSince) ? roamAvoid : nullptr;
  int pick = roamChoose(current, currentRssi, list, n, avoid);
  if(pick < 0) return;
  log("WiFi: roaming from %s (%ddb, ch %d) to %s (%ddb, ch %d)", WiFi.BSSIDstr().c_str(), currentRssi, WiFi.channel(),
      WiFi.BSSIDstr(pick).c_str(), list[pick].rssi, list[pick].channel);
  String psk = WiFi.psk();
  WiFi.scanDelete();
  memcpy(roamTarget, list[pick].bssid, 6);
  roamLastMove = UTCtime();
  roamLocked = true;
  WiFi.persistent(false);               // a move isn't a new configuration: don't write flash
  WiFi.begin(ssid.c_str(), psk.c_str(), list[pick].channel, list[pick].bssid);
  WiFi.persistent(true);
}

        // A scan in progress: take its results, or give up on it. Results are always freed.

static void roamPollScan(bool connected){
  int found = WiFi.scanComplete();
  if(found == WIFI_SCAN_RUNNING){
    if( ! roamScanOverdue(UTCtime(), roamLastScan)) return;
    log("WiFi: roaming scan didn't complete, abandoned.");
  }
  roamScanning = false;
  if(connected && found > 0) roamScanDone(found);
  WiFi.scanDelete();
}

void wifiRoamConnected(){
  if(roamScanning){
    roamPollScan(true);
    return;
  }
  if(WiFi.scanComplete() >= 0) WiFi.scanDelete();   // results of a scan given up on that finished later
  if( ! roamShouldScan(wifiRoam, WiFi.RSSI(), UTCtime(), roamLastScan, roamLastMove)) return;
  roamLastScan = UTCtime();
  roamScanning = WiFi.scanNetworks(true, false) == WIFI_SCAN_RUNNING;     // async; polled above
}

void wifiRoamDisconnected(uint32_t disconnectedSeconds){
  if(roamScanning) roamPollScan(false);
  if( ! roamLocked || disconnectedSeconds < WIFI_ROAM_UNLOCK_S) return;
  roamLocked = false;
  memcpy(roamAvoid, roamTarget, 6);
  roamAvoidSince = UTCtime();
  log("WiFi: access point from the last roam not reachable, reconnecting to any (skipping it for %d hours).",
      WIFI_ROAM_AVOID_S / 3600);
  String ssid = WiFi.SSID();
  String psk = WiFi.psk();
  WiFi.persistent(false);
  WiFi.begin(ssid.c_str(), psk.c_str());          // no BSSID: any access point of the network
  WiFi.persistent(true);
}
