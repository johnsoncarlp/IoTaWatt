// Host test stand-in for Arduino String: std::string with the subset of the Arduino API the firmware uses.
#pragma once
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cctype>

class String {
 public:
  String() {}
  String(const char* s) : _s(s ? s : "") {}
  String(const std::string& s) : _s(s) {}
  String(char c) : _s(1, c) {}
  String(int v) : _s(std::to_string(v)) {}
  String(unsigned int v) : _s(std::to_string(v)) {}
  String(long v) : _s(std::to_string(v)) {}
  String(unsigned long v) : _s(std::to_string(v)) {}
  String(double v, unsigned char decimals = 2) { char b[64]; snprintf(b, sizeof b, "%.*f", decimals, v); _s = b; }
  const char* c_str() const { return _s.c_str(); }
  unsigned int length() const { return _s.size(); }
  char charAt(unsigned int i) const { return i < _s.size() ? _s[i] : 0; }
  char operator[](unsigned int i) const { return charAt(i); }
  int indexOf(char c, unsigned int from = 0) const { auto p = _s.find(c, from); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const String& t, unsigned int from = 0) const { auto p = _s.find(t._s, from); return p == std::string::npos ? -1 : (int)p; }
  int lastIndexOf(char c) const { auto p = _s.rfind(c); return p == std::string::npos ? -1 : (int)p; }
  String substring(unsigned int from) const { return from >= _s.size() ? String() : String(_s.substr(from)); }
  String substring(unsigned int from, unsigned int to) const {
    if (from > to) std::swap(from, to);
    if (from >= _s.size()) return String();
    return String(_s.substr(from, std::min<size_t>(to, _s.size()) - from));
  }
  void remove(unsigned int index) { if (index < _s.size()) _s.erase(index); }
  void remove(unsigned int index, unsigned int count) { if (index < _s.size()) _s.erase(index, count); }
  void toLowerCase() { for (auto& c : _s) c = tolower((unsigned char)c); }
  void toUpperCase() { for (auto& c : _s) c = toupper((unsigned char)c); }
  void trim() { size_t a = _s.find_first_not_of(" \t\r\n"); size_t b = _s.find_last_not_of(" \t\r\n"); _s = a == std::string::npos ? "" : _s.substr(a, b - a + 1); }
  bool equals(const String& o) const { return _s == o._s; }
  bool equalsIgnoreCase(const String& o) const { return strcasecmp(_s.c_str(), o._s.c_str()) == 0; }
  bool startsWith(const String& p) const { return _s.compare(0, p._s.size(), p._s) == 0; }
  bool endsWith(const String& p) const { return _s.size() >= p._s.size() && _s.compare(_s.size() - p._s.size(), p._s.size(), p._s) == 0; }
  long toInt() const { return atol(_s.c_str()); }
  float toFloat() const { return (float)atof(_s.c_str()); }
  String& operator+=(const String& o) { _s += o._s; return *this; }
  String& operator+=(const char* o) { if (o) _s += o; return *this; }
  String& operator+=(char c) { _s += c; return *this; }
  String& operator+=(int v) { _s += std::to_string(v); return *this; }
  bool concat(const char* o) { if (o) _s += o; return true; }
  friend String operator+(String a, const String& b) { a += b; return a; }
  friend String operator+(String a, const char* b) { a += b; return a; }
  friend String operator+(const char* a, const String& b) { return String(a) + b; }
  bool operator==(const String& o) const { return _s == o._s; }
  bool operator==(const char* o) const { return _s == (o ? o : ""); }
  bool operator!=(const String& o) const { return _s != o._s; }
  bool operator!=(const char* o) const { return !(*this == o); }
  bool operator<(const String& o) const { return _s < o._s; }
  // ArduinoJson's String support reads and writes through these
  size_t write(uint8_t c) { _s += (char)c; return 1; }
  size_t write(const uint8_t* b, size_t n) { _s.append((const char*)b, n); return n; }
  const std::string& str() const { return _s; }
 private:
  std::string _s;
};
