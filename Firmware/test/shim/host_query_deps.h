// Host test stand-ins for CSVquery (/query): request arguments (fake web server), current time, and
// logReadKey() on the current log only, the real function's path when the history log isn't open.
#pragma once
#include <map>
#include "host_config_deps.h"

struct HostServer {
  std::map<std::string, std::string> args;
  bool hasArg(const char* k) { return args.count(k); }
  bool hasArg(const String& k) { return hasArg(k.c_str()); }
  String arg(const char* k) { auto it = args.find(k); return it == args.end() ? String() : String(it->second); }
  String arg(const String& k) { return arg(k.c_str()); }
};
inline HostServer server;

inline uint32_t hostUTC = 1795561084;            // "now" for relative times
inline uint32_t UTCtime() { return hostUTC; }
inline uint32_t UTCtime(uint32_t local) { return local - hostLocalOffset; }   // fixed offset, no DST in these tests
inline int logReadKey(IotaLogRecord* r) { return Current_log.readKey(r); }
