// Host test stand-in for IotaWatt.h: the globals and helpers that units under test use, without the
// ESP8266, WiFi, web server or hardware. Selected in device sources by IOTAWATT_HOST_TEST (native env).
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>
#include <algorithm>
#include "Arduino.h"
#include "host_string.h"
#include "host_fs.h"

using std::max;
using std::min;
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#define PSTR(s) (s)
#define F(s) (s)

// Messages the firmware logs: tests can inspect them.
inline std::vector<std::string> hostLog;
inline void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
inline void log(const char* fmt, ...) {
  char b[512]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); hostLog.push_back(b);
}

struct HostSerial {
  int printf(const char*, ...) { return 0; }
  int printf_P(const char*, ...) { return 0; }
  void print(...) {}
  void println(...) {}
};
inline HostSerial Serial;

struct HostESP {
  int restarts = 0;
  void restart() { restarts++; }       // the device never returns from this; tests check the count
  uint32_t getFreeHeap() { return 40000; }
};
inline HostESP ESP;

#define LED_DUMPING_LOG 0
inline void setLedCycle(int) {}
inline void endLedCycle() {}

inline uint32_t hostLocalTime = 1795561084;
inline uint32_t localTime() { return hostLocalTime; }
struct DateTime {
  explicit DateTime(uint32_t t) { time_t tt = t; gmtime_r(&tt, &_tm); }
  int year() const { return _tm.tm_year + 1900; }
  int month() const { return _tm.tm_mon + 1; }
  int day() const { return _tm.tm_mday; }
  int hour() const { return _tm.tm_hour; }
  int minute() const { return _tm.tm_min; }
  int second() const { return _tm.tm_sec; }
  struct tm _tm{};
};

inline char* charstar(const char* str1, const char* str2 = nullptr) {
  size_t l1 = str1 ? strlen(str1) : 0, l2 = str2 ? strlen(str2) : 0;
  if (!(l1 + l2)) return nullptr;
  char* p = new char[l1 + l2 + 1];
  if (str1) strcpy(p, str1); else p[0] = 0;
  if (str2) strcpy(p + l1, str2);
  return p;
}
inline char* charstar(const String& s) { return charstar(s.c_str()); }
