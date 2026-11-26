// Host test stand-ins for the web server (webServer.cpp, graphSave.cpp): a fake ESP8266WebServer that
// records responses, IPAddress, sampling and update hooks, auth storage, and the status globals.
// Builds on the uploader stand-ins so /status can report real uploader objects.
#pragma once
#include "host_uploader_deps.h"
#include "host_xbuf.h"
#include "host_channel_deps.h"
#include "uploaders/Uploader.h"

#ifndef IOTAWATT_VERSION
#define IOTAWATT_VERSION "02_08_04"
#endif
#define IOTA_SYSTEM_DIR       "/iotawatt/"            // as IotaWatt.h
#define IOTA_CURRENT_LOG_PATH "/iotawatt/iotalog.log"
#define IOTA_HISTORY_LOG_PATH "/iotawatt/histlog.log"
#define IOTA_AUTH_PATH        "/iotawatt/auth.txt"
#define IOTA_CONFIG_PATH      "/config.txt"
#define IOTA_CONFIG_NEW_PATH  "/config+1.txt"

// ---- fake web server ----
enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_HEAD, HTTP_POST, HTTP_PUT, HTTP_PATCH, HTTP_DELETE, HTTP_OPTIONS };
#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)
enum HTTPUploadStatus { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
struct HTTPUpload { HTTPUploadStatus status; String filename, name, type; size_t totalSize = 0, currentSize = 0; uint8_t buf[2048]; };
struct HostClient {
  bool stopped = false; std::string written;
  void stop() { stopped = true; }
  IPAddress ip = IPAddress(192, 168, 1, 10);       // tests may change the caller
  IPAddress remoteIP() { return ip; }
  size_t write(const char* b, size_t n) { written.append(b, n); return n; }
  size_t write(const uint8_t* b, size_t n) { written.append((const char*)b, n); return n; }
  size_t write(File& f) { uint8_t b[512]; int n; size_t t = 0; while ((n = f.read(b, sizeof b)) > 0) { written.append((char*)b, n); t += n; } return t; }
};
typedef HostClient WiFiClient;
struct HostResponse { int code; std::string type, body; };
class HostWebServer {
 public:
  std::map<std::string, std::string> argv, reqHeaders, sentHeaders;
  std::string _uri = "/"; HTTPMethod _method = HTTP_GET;
  std::vector<HostResponse> responses; std::string content; size_t contentLength = 0;
  HostClient _client; HTTPUpload _upload;
  uint32_t millisPerChunk = 0;                    // models the time each streamed chunk takes
  void reset() { millisPerChunk = 0; argv.clear(); reqHeaders.clear(); sentHeaders.clear(); responses.clear(); content.clear(); _client = HostClient(); _uri = "/"; _method = HTTP_GET; }
  bool hasArg(const char* k) { return argv.count(k); }
  bool hasArg(const String& k) { return hasArg(k.c_str()); }
  String arg(const char* k) { auto it = argv.find(k); return it == argv.end() ? String() : String(it->second); }
  String arg(const String& k) { return arg(k.c_str()); }
  int args() { return (int)argv.size(); }
  String uri() { return String(_uri); }
  HTTPMethod method() { return _method; }
  void send(int code, const char* type, const String& body) { responses.push_back({code, type ? type : "", body.str()}); }
  void send(int code, const String& type, const String& body) { send(code, type.c_str(), body); }
  void send(int code, const char* type, const char* body) { send(code, type, String(body)); }
  void send(int code, const char* type = "") { send(code, type, String()); }
  void send_P(int code, const char* type, const char* data, size_t n) { responses.push_back({code, type ? type : "", std::string(data, n)}); }
  void sendHeader(const String& n, const String& v, bool = false) { sentHeaders[n.str()] = v.str(); }
  void setContentLength(size_t n) { contentLength = n; }
  void sendContent(const String& s) { content += s.str(); hostMillis += millisPerChunk; }
  void sendContent(const char* s, size_t n) { content.append(s, n); hostMillis += millisPerChunk; }
  HostClient& client() { return _client; }
  HTTPUpload& upload() { return _upload; }
  bool hasHeader(const char* n) { return reqHeaders.count(n); }
  String header(const char* n) { auto it = reqHeaders.find(n); return it == reqHeaders.end() ? String() : String(it->second); }
  template <class F> size_t streamFile(F& f, const String& type) { responses.push_back({200, type.str(), ""}); return _client.write(f); }
  template <class... A> void on(A&&...) {}
  template <class... A> void onNotFound(A&&...) {}
  template <class... A> void collectHeaders(A&&...) {}
  void begin() {}
};
inline HostWebServer server;
inline bool serverAvailable = true;
inline bool webuiFromSD = false;

// ---- status globals ----
inline IotaLogRecord statRecord;
inline float     samplesPerCycle = 640, cycleSampleRate = 40;
inline uint32_t  programStartTime = 0, wifiConnectTime = 0;
inline bool      RTClowBat = false, localAccess = false;
inline uint8_t*  adminH1 = nullptr;
inline uint8_t*  userH1 = nullptr;
inline char*     updateClass = (char*)"NONE";
inline ScriptSet* outputs = new ScriptSet();

// ---- hooks the handlers call (recorded) ----
inline int hostSavedPwds = 0;
inline bool authSavePwds() { hostSavedPwds++; return true; }
inline bool hostAuthResult = true;
inline void requestAuth() { server.send(401, "text/plain", "auth"); }
inline String calcH1(const char* user, const char* realm, const char* pwd) {
  // Stand-in for MD5(user:realm:pwd) (the device's ROM MD5 isn't on the host): any 32 hex digits that
  // depend on all three inputs serve the handler logic under test.
  std::string s = std::string(user ? user : "") + ":" + (realm ? realm : "") + ":" + (pwd ? pwd : "");
  uint64_t h = 1469598103934665603ULL, g = 1099511628211ULL; for (char c : s) { h ^= (uint8_t)c; h *= g; g = g * 31 + 7; }
  char out[33]; snprintf(out, sizeof out, "%016llx%016llx", (unsigned long long)h, (unsigned long long)(h * 0x9E3779B97F4A7C15ULL)); return String(out);
}
inline float hostVrms = 121.5f;
inline int   hostSampleCycleRc = 0;
inline float sampleVoltage(uint8_t, float) { return hostVrms; }
inline float samplePhase(uint8_t, uint8_t, int) { return 1.25f; }
inline int   sampleCycle(IotaInputChannel*, IotaInputChannel*, int = 0, int = 0) { return hostSampleCycleRc; }
inline void  getSamples() { server.send(200, "text/plain", "samples"); }
inline bool  installUpdate(String) { return false; }
inline bool  unpackUpdate(String) { return false; }
inline uint32_t getFeedData() { return 0; }          // GetFeedData.cpp: uint32_t getFeedData()
inline bool getNewConfig = false;
inline uint8_t configSHA256[32];
inline std::map<std::string, std::string> hostSpiffs;
inline String spiffsRead(const char* p) { return hostSpiffs.count(p) ? String(hostSpiffs[p]) : String(); }
inline bool   spiffsRemove(const char* p) { return hostSpiffs.erase(p); }
inline String spiffsDirectory(String) { return String("[]"); }

// ---- uploaders reported by /status ----
inline std::vector<Uploader*> hostWebUploaders;
inline const char* hostWebUploaderNames[] = {"Emoncms", "influxDB", "influxDB2", nullptr};
inline char** get_uploader_list() { return (char**)hostWebUploaderNames; }
inline Uploader* get_uploader_instance(const char* id) {
  for (auto* u : hostWebUploaders) if (u && strcmp(u->id(), id) == 0) return u; return nullptr;
}
class PVoutput_uploader {
 public:
  void getStatusJson(JsonObject& s) { s["id"] = "PVoutput"; s["status"] = "stopped"; }
};
inline PVoutput_uploader* PVoutput = nullptr;
inline int logReadKey(IotaLogRecord* r) { return Current_log.readKey(r); }   // history log not open
