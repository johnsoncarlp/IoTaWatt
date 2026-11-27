#ifndef wifiSwitch_h
#define wifiSwitch_h

/*******************************************************************************************************
 * Changing the Wi-Fi network of a working IoTaWatt (Tools → Wi-Fi), without the setup hotspot (F18)
 *
 * POST /wifi (admin) with the new ssid and password: the device keeps its current network and switches.
 * Connected to the new one within WIFI_SWITCH_TIMEOUT_S: it stays (and the new credentials are stored).
 * Not: it goes back to the old network, so a mistyped password doesn't leave it unreachable. Roaming
 * waits meanwhile. GET /wifi/scan lists nearby networks for the page (asynchronous: 202 while scanning).
 ******************************************************************************************************/

#include <stdint.h>
#include <string.h>

#define WIFI_SWITCH_TIMEOUT_S 60

// A network name of 1-32 bytes; a password empty (open network), 8-63 characters, or 64 hex digits
// (WPA2's limits). Pure (host tested).
inline bool wifiCredentialsValid(const char* ssid, const char* psk){
  if( ! ssid || ! psk) return false;
  size_t s = strlen(ssid), p = strlen(psk);
  if(s < 1 || s > 32) return false;
  if(p == 0) return true;
  if(p >= 8 && p <= 63) return true;
  if(p != 64) return false;
  for(size_t i = 0; i < 64; i++){
    char c = psk[i];
    if( ! ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
  }
  return true;
}

enum wifiSwitchStep { wifiSwitchWaiting, wifiSwitchDone, wifiSwitchRevert };

// While switching: connected to the new network = done; out of time = go back. Pure (host tested).
inline wifiSwitchStep wifiSwitchCheck(uint32_t now, uint32_t started, bool connectedToNew){
  if(connectedToNew) return wifiSwitchDone;
  if(now - started >= WIFI_SWITCH_TIMEOUT_S) return wifiSwitchRevert;
  return wifiSwitchWaiting;
}

#ifndef IOTAWATT_HOST_TEST
#include <Arduino.h>
bool   wifiSwitchStart(const String& ssid, const String& psk);   // false if a switch is under way
bool   wifiSwitching();
String wifiScanJson();                  // the scan's networks as JSON, or "" while it runs (started if needed)
#endif

#endif
