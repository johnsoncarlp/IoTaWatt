#include "IotaWatt.h"
#include "ESP8266mDNS.h"
#include <ESP8266LLMNR.h>

struct tcp_pcb {
  uint32_t ip_pcba;
  uint32_t ip_pcbb;
  uint32_t ip_pcbc;
  tcp_pcb* next;
};
extern struct tcp_pcb* tcp_tw_pcbs;
extern "C" void tcp_abort(struct tcp_pcb* pcb);

#include "wifiSwitch.h"

        // A switch to another network (wifiSwitch.h): the old one is kept here to go back to.
static bool     switching = false;
static String   switchSsid, oldSsid, oldPsk;
static uint32_t switchStarted = 0;

bool wifiSwitching(){ return switching; }

bool wifiSwitchStart(const String& ssid, const String& psk){
  if(switching) return false;
  oldSsid = WiFi.SSID();
  oldPsk = WiFi.psk();
  switchSsid = ssid;
  switchStarted = UTCtime();
  switching = true;
  log("WiFi: switching from %s to %s (back to %s if not connected within %d s).", oldSsid.c_str(), ssid.c_str(),
      oldSsid.c_str(), WIFI_SWITCH_TIMEOUT_S);
  WiFi.persistent(true);                          // the new network is the one to use from now on
  WiFi.begin(ssid.c_str(), psk.c_str());
  return true;
}

static void wifiSwitchPoll(){
  bool onNew = WiFi.status() == WL_CONNECTED && WiFi.SSID() == switchSsid;
  switch(wifiSwitchCheck(UTCtime(), switchStarted, onNew)){
    case wifiSwitchWaiting: return;
    case wifiSwitchDone:
      log("WiFi: switched to %s.", switchSsid.c_str());
      break;
    case wifiSwitchRevert:
      log("WiFi: couldn't connect to %s within %d s, back to %s.", switchSsid.c_str(), WIFI_SWITCH_TIMEOUT_S, oldSsid.c_str());
      WiFi.persistent(true);
      WiFi.begin(oldSsid.c_str(), oldPsk.c_str());
      break;
  }
  switching = false;
  oldPsk = "";
}

        // Nearby networks for the Wi-Fi page: one entry per name, the strongest; asynchronous.
String wifiScanJson(){
  int found = WiFi.scanComplete();
  if(found == WIFI_SCAN_RUNNING) return "";
  if(found < 0){
    WiFi.scanNetworks(true, false);
    return "";
  }
  JsonDocument doc;
  JsonArray list = doc.to<JsonArray>();
  for(int i = 0; i < found; i++){
    String ssid = WiFi.SSID(i);
    if( ! ssid.length()) continue;
    bool seen = false;
    for(JsonObject n : list){
      if(ssid.equals(n["ssid"].as<const char*>())){
        seen = true;
        if(WiFi.RSSI(i) > n["rssi"].as<int>()){ n["rssi"] = WiFi.RSSI(i); n["channel"] = WiFi.channel(i); }
      }
    }
    if(seen) continue;
    JsonObject n = list.add<JsonObject>();
    n["ssid"] = ssid;
    n["rssi"] = WiFi.RSSI(i);
    n["channel"] = WiFi.channel(i);
    n["open"] = WiFi.encryptionType(i) == ENC_TYPE_NONE;
  }
  WiFi.scanDelete();
  String out;
  serializeJson(doc, out);
  return out;
}

uint32_t WiFiService(struct serviceBlock* _serviceBlock) {
  static uint32_t lastDisconnect = UTCtime();       // Time of last disconnect
  const uint32_t restartInterval = 60*60;           // Restart if disconnected this many seconds
  static bool mDNSstarted = false;
  static bool LLMNRstarted = false;

  trace(T_WiFi,0);
  if(WiFi.status() == WL_CONNECTED){
    trace(T_WiFi,1);
    if(!wifiConnectTime){
      trace(T_WiFi,1);
      wifiConnectTime = UTCtime();
      localIP = WiFi.localIP();
      gatewayIP = WiFi.gatewayIP();
      subnetMask = WiFi.subnetMask();
      WiFi.hostname(deviceName);
      log("WiFi connected. SSID=%s, IP=%s, channel=%d, RSSI %ddb", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.channel(), WiFi.RSSI());
    }
    if(switching) wifiSwitchPoll();     // a network change from the Wi-Fi page (wifiSwitch.h)
    else wifiRoamConnected();           // move to a clearly stronger access point (wifiRoam.h)
    if( ! mDNSstarted){
      if (MDNS.begin(deviceName)) {
        MDNS.addService("http", "tcp", 80);
        mDNSstarted = true;
      }
    }
    else {
      MDNS.update();
    }
    if( ! LLMNRstarted){
      if (LLMNR.begin(deviceName)){
        LLMNRstarted = true;
      } 
    }
  }
  else {
    trace(T_WiFi,2);
    if(wifiConnectTime){
      trace(T_WiFi,2);
      wifiConnectTime = 0;
      lastDisconnect = UTCtime();
      log("WiFi disconnected.");
    }
    else {
      if(switching) wifiSwitchPoll();
      else wifiRoamDisconnected(UTCtime() - lastDisconnect);   // unlock a roam's access point (wifiRoam.h)
      if((UTCtime() - lastDisconnect) >= restartInterval){
        log("WiFi disconnected more than %d minutes, restarting.", restartInterval / 60);
        delay(500);
        ESP.restart();
      }
    }
  }

    // Check for degraded heap.

  trace(T_WiFi,10);
  if(ESP.getFreeHeap() < 7000){
    trace(T_WiFi,10);
    log("Heap memory has degraded below safe minimum, restarting.");
    delay(500);
    ESP.restart();
  }

      // Check for expired HTTP request.

  trace(T_WiFi,20);
  for(int i=0; i<HTTPrequestMax; i++){
    trace(T_WiFi,21,i);
    if(HTTPrequestStart[i] && (millis() - HTTPrequestStart[i]) > 900000UL){
      trace(T_WiFi,22,i);
      log("Incomplete HTTP request detected, id %d, restarting.", HTTPrequestId[i]);
      delay(500);
      ESP.restart();
    }
  }    

      // Purge any timed out authorization sessions.

  trace(T_WiFi,30);
  purgeAuthSessions();
 
  trace(T_WiFi,99);
  return UTCtime() + 1;  
}

uint32_t HTTPreserve(uint16_t id, bool lock){
  trace(T_WiFi,100,id);
  if(HTTPrequestFree == 0 || HTTPlock) return 0;
  HTTPrequestFree--;
  for(int i=0; i<HTTPrequestMax; i++){
    trace(T_WiFi,101,i);
    if(HTTPrequestStart[i] == 0){
      HTTPrequestStart[i] = millis();
      HTTPrequestId[i] = id;
      if(lock){
        HTTPlock = HTTPrequestStart[i];
      }
      return HTTPrequestStart[i];
    }
  }
  return 0;
}

void HTTPrelease(uint32_t HTTPtoken){
  trace(T_WiFi,110);
  for(int i=0; i<HTTPrequestMax; i++){
    trace(T_WiFi,110,i);
    if(HTTPrequestStart[i] == HTTPtoken){
      HTTPrequestStart[i] = 0;
      HTTPrequestFree++;
      if(HTTPtoken == HTTPlock){
        HTTPlock = 0;
      }
    }
  }
}