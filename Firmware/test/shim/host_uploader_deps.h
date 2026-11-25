// Host test stand-ins for the uploaders (Uploader, influxDB v1/v2, Emoncms) and the real utilities.cpp:
// a fake asyncHTTPrequest that records each request and returns scripted responses, WiFi, the
// scheduler's NewService, HTTP tokens, a deterministic random(), and the device globals they use.
#pragma once
#define IOTAWATT_HOST_REAL_UTILITIES                 // this suite builds the real utilities.cpp
#include "trace.h"                                   // T_* ids; must precede the no-op trace() macro
#include <deque>
#include <functional>
#include <map>
#include "host_iotawatt.h"
#include "host_xbuf.h"
#include "host_script_deps.h"
#include "jsonUtil.h"
#include "IotaLog.h"
#include "timeServices.h"
#include "utilities.h"
#include <SHA256.h>
#include <AES.h>
#include <Ed25519.h>

// ---- globals ----
inline int32_t   localTimeDiff = 0;
inline tzRule*   timezoneRule = nullptr;
inline char*     HTTPSproxy = nullptr;
inline IotaLog   Current_log;
inline IotaLog   History_log(256, 60, 3650, 0);
inline int32_t   uploaderBufferLimit = 3000;
inline int32_t   uploaderBufferTotal = 6000;
inline uint32_t  bingoTime = 0xFFFFFFFF;            // micros() is 0 on the host: never "time to sample"
inline uint32_t  hostUTC = 1795561084;
inline uint32_t  UTCtime() { return hostUTC; }
inline uint32_t  UTCtime(uint32_t local) { return local2UTC(local); }
inline uint8_t   publicKey[32];
inline char*     deviceName = (char*)"IotaWatt";
inline const char hexcodes_P[] = "0123456789abcdef";                 // as common.cpp
inline const char base64codes_P[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
inline bool      spiffsFileExists(const char*) { return false; }
inline size_t    spiffsWrite(const char*, String, bool = false) { return 0; }
inline size_t    spiffsWrite(const char*, uint8_t*, size_t len, bool = false) { return len; }

// ---- WiFi, HTTP tokens, scheduler ----
struct HostWiFi {
  bool connected = true; int disconnects = 0;
  bool isConnected() { return connected; }
  String SSID() { return String("TestNet"); }
  IPAddress localIP() { return IPAddress(192, 168, 1, 50); }
  int channel() { return 6; }
  int RSSI() { return -61; }
  String macAddress() { return String("AA:BB:CC:DD:EE:FF"); }
  void disconnect(bool = false) { disconnects++; }
};
inline HostWiFi WiFi;
inline int hostHttpTokens = 0;
inline uint32_t HTTPreserve(uint16_t, bool = false) { return ++hostHttpTokens; }
inline void HTTPrelease(uint32_t) {}

enum priorities : uint8_t { priorityLow = 1, priorityMed = 2, priorityHigh = 3 };
struct serviceBlock;
typedef std::function<uint32_t(struct serviceBlock*)> Service;
struct serviceBlock {
  serviceBlock* next = nullptr; uint32_t scheduleTime = 1; Service service; void* serviceParm = nullptr;
  priorities priority = priorityMed; uint8_t taskID = 0;
};
inline std::vector<serviceBlock*> hostServices;
inline serviceBlock* NewService(Service s, const uint8_t taskID = 0, void* parm = 0) {
  auto* b = new serviceBlock(); b->service = s; b->taskID = taskID; b->serviceParm = parm; hostServices.push_back(b); return b;
}

// ---- deterministic random() (Emoncms IVs) ----
inline uint32_t hostRandomSeed = 1;
inline long random(long howbig) { hostRandomSeed = hostRandomSeed * 1103515245u + 12345u; return (hostRandomSeed >> 16) % howbig; }

// ---- fake HTTP ----
struct HostHttpRequest { std::string method, url, body; std::map<std::string, std::string> headers; };
struct HostHttpReply { int code; std::string text; };
inline std::vector<HostHttpRequest> hostHttpLog;
inline std::deque<HostHttpReply> hostHttpReplies;
class asyncHTTPrequest {
 public:
  bool open(const char* method, const char* url) { _cur = HostHttpRequest(); _cur.method = method; _cur.url = url; _state = 1; return true; }
  void setReqHeader(const char* n, const char* v) { _cur.headers[n] = v ? v : ""; }
  void setTimeout(int) {}
  void setDebug(bool) {}
  bool debug() { return false; }
  bool send() { return finish(); }
  bool send(const char* body) { _cur.body = body ? body : ""; return finish(); }
  bool send(xbuf* b, size_t n) { std::string s(n, 0); b->read((uint8_t*)&s[0], n); _cur.body = s; return finish(); }
  int  readyState() { return _state; }
  int  responseHTTPcode() { return _code; }
  String responseText() { return String(_text); }
 private:
  bool finish() {
    hostHttpLog.push_back(_cur);
    if (hostHttpReplies.empty()) { _code = -4; _text = ""; }     // no reply scripted: like a timeout
    else { _code = hostHttpReplies.front().code; _text = hostHttpReplies.front().text; hostHttpReplies.pop_front(); }
    _state = 4; return true;
  }
  HostHttpRequest _cur; int _state = 0, _code = 0; std::string _text;
};
