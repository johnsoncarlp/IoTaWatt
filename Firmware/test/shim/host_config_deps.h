// Host test stand-ins for what setConfig.cpp uses beyond scripts: device globals, paths, the uploader
// registry and PVOutput (fakes that record their config), SPIFFS (in memory) and hashFile (no-op).
#pragma once
#include <map>
#include "host_iotawatt.h"
#include "host_script_deps.h"
#include "host_channel_deps.h"
#include "IotaLog.h"
#include "timeServices.h"

#define IOTA_CONFIG_PATH      "/config.txt"
#define IOTA_CONFIG_NEW_PATH  "/config+1.txt"
#define IOTA_CONFIG_OLD_PATH  "/config-1.txt"
#define IOTA_CONFIG_BAD_PATH  "/config-bad.txt"
#define IOTA_TABLE_PATH       "/tables.txt"
#define MAXINPUTS 15
#define pin_CS_ADC0 0
#define pin_CS_ADC1 2

inline char*     updateClass = nullptr;
inline int32_t   localTimeDiff = 0;
inline tzRule*   timezoneRule = nullptr;
inline char*     deviceName = nullptr;
inline char*     ntpServers = nullptr;
inline char*     HTTPSproxy = nullptr;
inline float     VrefVolts = 2.5;
inline uint8_t   ADC_selectPin[2] = {pin_CS_ADC0, pin_CS_ADC1};
inline bool      hasRTC = true;
inline uint8_t   configSHA256[32];
inline int16_t*  masterPhaseArray = nullptr;
inline ScriptSet* outputs = new ScriptSet();
inline IotaLog   Current_log;

String JsonSummary(File file, int depth);
char*  JsonDetail(File file, JsonArray locator);
#ifndef IOTAWATT_HOST_REAL_UTILITIES
inline void hashFile(uint8_t* sha, File) { memset(sha, 0, 32); }
#endif

// SPIFFS in memory
inline std::map<std::string, std::string> hostSpiffs;
inline bool   spiffsFileExists(const char* p) { return hostSpiffs.count(p); }
inline String spiffsRead(const char* p) { auto it = hostSpiffs.find(p); return it == hostSpiffs.end() ? String() : String(it->second); }
inline size_t spiffsWrite(const char* p, String c, bool append = false) {
  if (append) hostSpiffs[p] += c.str(); else hostSpiffs[p] = c.str(); return c.length();
}

// Uploaders: record the config each one is given.
class Uploader {
 public:
  std::string name, lastConfig; bool accept = true, ended = false;
  virtual ~Uploader() {}
  virtual bool config(const char* json) { lastConfig = json ? json : ""; return accept; }
  virtual void end() { ended = true; }
};
inline const char* hostUploaderNames[] = {"Emoncms", "influxDB", "influxDB2", nullptr};
inline std::map<std::string, Uploader*> hostUploaders;
inline bool hostUploaderAccept = true;
inline char** get_uploader_list() { return (char**)hostUploaderNames; }
inline Uploader* new_uploader(const char* id) {
  auto& u = hostUploaders[id]; if (!u) { u = new Uploader(); u->name = id; } u->accept = hostUploaderAccept; return u;
}
inline bool delete_uploader(const char* id) {
  auto it = hostUploaders.find(id); if (it == hostUploaders.end()) return false;
  delete it->second; hostUploaders.erase(it); return true;
}

class PVoutput_uploader {
 public:
  std::string lastConfig; bool ended = false;
  bool config(const char* json) { lastConfig = json ? json : ""; return true; }
  void end() { ended = true; }
};
inline PVoutput_uploader* PVoutput = nullptr;
