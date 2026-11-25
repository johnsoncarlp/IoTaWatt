// Host test stand-in for asyncHTTPrequest's xbuf (a growable byte buffer with Print-style output):
// only what the units under test use.
#pragma once
#include <cstdarg>
#include <cstdio>
#include <string>
#include <type_traits>
#include "host_string.h"

class xbuf {
 public:
  explicit xbuf(uint16_t = 64) {}
  size_t print(char c) { _s += c; return 1; }
  size_t print(const char* s) { if (s) _s += s; return s ? strlen(s) : 0; }
  size_t print(const String& s) { _s += s.str(); return s.length(); }
  template <class T, class = typename std::enable_if<std::is_integral<T>::value>::type>
  size_t print(T v) { std::string t = std::to_string(v); _s += t; return t.size(); }
  size_t write(const char* s) { return print(s); }
  template <class T> size_t println(const T& v) { size_t n = print(v); _s += "\r\n"; return n + 2; }
  size_t println() { _s += "\r\n"; return 2; }
  size_t write(const String& s) { return print(s); }
  size_t write(char c) { return print(c); }
  size_t write(uint8_t c) { _s += (char)c; return 1; }
  void   flush() { _s.clear(); }
  String peekString(int n) { return String(_s.substr(0, n)); }
  size_t write(const uint8_t* b, size_t n) { _s.append((const char*)b, n); return n; }
  size_t read(uint8_t* buf, size_t n) { n = std::min(n, _s.size()); memcpy(buf, _s.data(), n); _s.erase(0, n); return n; }
  size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    va_list a; va_start(a, fmt); int n = vsnprintf(nullptr, 0, fmt, a); va_end(a);
    if (n <= 0) return 0;
    std::string b(n + 1, 0); va_start(a, fmt); vsnprintf(&b[0], n + 1, fmt, a); va_end(a);
    _s.append(b.data(), n); return n;
  }
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
