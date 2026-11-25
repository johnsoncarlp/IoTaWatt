// Host test stand-in for asyncHTTPrequest's xbuf (a growable byte buffer with Print-style output):
// only what the units under test use.
#pragma once
#include <cstdarg>
#include <cstdio>
#include <string>
#include "host_string.h"

class xbuf {
 public:
  explicit xbuf(uint16_t = 64) {}
  size_t print(char c) { _s += c; return 1; }
  size_t print(const char* s) { _s += s; return strlen(s); }
  size_t printf_P(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    char b[256]; va_list a; va_start(a, fmt); int n = vsnprintf(b, sizeof b, fmt, a); va_end(a);
    if (n > 0) _s.append(b, std::min<size_t>(n, sizeof b - 1));
    return n;
  }
  size_t available() const { return _s.size(); }
  String readString() { String r(_s); _s.clear(); return r; }
  String readString(int n) { String r(_s.substr(0, n)); _s.erase(0, n); return r; }
 private:
  std::string _s;
};
