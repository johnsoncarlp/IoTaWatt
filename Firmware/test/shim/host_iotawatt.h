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
#define PROGMEM
#define PGM_P const char*
#define strlen_P strlen
#define strcpy_P strcpy
#define memcpy_P memcpy
#define sprintf_P sprintf
#define snprintf_P snprintf
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define constrain(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#define F(s) (s)
class __FlashStringHelper;                      // only named by overloads in the real utilities
#define FPSTR(s) (s)

// Messages the firmware logs: tests can inspect them.
inline std::vector<std::string> hostLog;
inline void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
inline void log(const char* fmt, ...) {
  char b[512]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); hostLog.push_back(b);
}

struct HostSerial {
  int printf(const char*, ...) { return 0; }
  int printf_P(const char*, ...) { return 0; }
  template <class... A> void print(A&&...) {}
  template <class... A> void println(A&&...) {}
};
inline void delay(unsigned long) {}
#define strcmp_P strcmp
inline HostSerial Serial;

struct IPAddress {
  uint8_t b[4] = {0, 0, 0, 0};
  IPAddress() {}
  IPAddress(uint8_t a, uint8_t c, uint8_t d, uint8_t e) { b[0] = a; b[1] = c; b[2] = d; b[3] = e; }
  String toString() const { char s[16]; snprintf(s, sizeof s, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]); return String(s); }
  operator uint32_t() const { return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }
};

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

#include "host_utilities_fakes.h"            // charstar, strcmp_ci (unless the real utilities.cpp is built)

// ---- used by IotaScript, IotaInputChannel and the units under test that need time ----
typedef uint32_t uint32;
#define trace(...) ((void)0)
inline int32_t hostLocalOffset = 0;                 // seconds east of UTC
inline uint32_t localTime(uint32_t utc) { return utc + hostLocalOffset; }
