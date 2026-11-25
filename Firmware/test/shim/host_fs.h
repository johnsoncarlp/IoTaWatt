// Host test stand-in for the ESP8266 SD card: an in-memory, sparse filesystem.
//
// Semantics follow the device (libraries/SD + SDFS + SdFat, core 3.1.2):
//   FILE_READ  -> read only, position 0.
//   FILE_WRITE -> "a+" -> O_RDWR | O_CREAT | O_AT_END: created if missing, starts at the end, but seek()
//                 before read/write is honoured (writes overwrite or extend; there is no append-on-write).
// Files are stored in 4 KB pages, so a test can create a multi-GB file cheaply; unwritten bytes read as
// zero, or come from a per-file generator (used to synthesize huge logs).
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <map>
#include <set>
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <algorithm>
#include "host_string.h"

#define FILE_READ  0
#define FILE_WRITE 1

struct HostFileData {
  static const uint64_t PAGE = 4096;
  uint64_t size = 0;
  std::map<uint64_t, std::vector<uint8_t>> pages;
  std::function<void(uint64_t pos, uint8_t* out, size_t len)> generator;   // fills unwritten bytes
  uint8_t byteAt(uint64_t pos) const {
    auto it = pages.find(pos / PAGE);
    if (it != pages.end()) return it->second[pos % PAGE];
    uint8_t b = 0;
    if (generator) generator(pos, &b, 1);
    return b;
  }
  void readAt(uint64_t pos, uint8_t* out, size_t len) const {
    while (len) {
      uint64_t pg = pos / PAGE; size_t off = pos % PAGE; size_t n = std::min<uint64_t>(len, PAGE - off);
      auto it = pages.find(pg);
      if (it != pages.end()) memcpy(out, it->second.data() + off, n);
      else if (generator) generator(pos, out, n);
      else memset(out, 0, n);
      pos += n; out += n; len -= n;
    }
  }
  void writeAt(uint64_t pos, const uint8_t* in, size_t len) {
    while (len) {
      uint64_t pg = pos / PAGE; size_t off = pos % PAGE; size_t n = std::min<uint64_t>(len, PAGE - off);
      auto it = pages.find(pg);
      if (it == pages.end()) {
        std::vector<uint8_t> page(PAGE);
        if (generator) generator(pg * PAGE, page.data(), PAGE);   // keep generated content around the write
        it = pages.emplace(pg, std::move(page)).first;
      }
      memcpy(it->second.data() + off, in, n);
      pos += n; in += n; len -= n;
    }
  }
};

class File {
 public:
  File() {}
  File(std::shared_ptr<HostFileData> d, bool writable, std::string name)
    : _d(d), _writable(writable), _name(name), _pos(writable ? d->size : 0) {}
  explicit operator bool() const { return (bool)_d; }
  int read() { uint8_t b; return read(&b, 1) == 1 ? b : -1; }
  int read(uint8_t* buf, size_t len) {
    if (!_d || _pos >= _d->size) return 0;
    size_t n = std::min<uint64_t>(len, _d->size - _pos);
    _d->readAt(_pos, buf, n); _pos += n; return (int)n;
  }
  size_t write(const uint8_t* buf, size_t len) {
    if (!_d || !_writable) return 0;
    _d->writeAt(_pos, buf, len); _pos += len; if (_pos > _d->size) _d->size = _pos; writes++; return len;
  }
  size_t write(const char* buf, size_t len) { return write((const uint8_t*)buf, len); }
  size_t write(uint8_t b) { return write(&b, 1); }
  bool seek(uint32_t pos) { if (!_d) return false; _pos = pos; return true; }
  uint32_t position() const { return (uint32_t)_pos; }
  uint32_t size() const { return _d ? (uint32_t)_d->size : 0; }     // 32-bit, as on the device
  int available() const { return _d && _pos < _d->size ? (int)std::min<uint64_t>(_d->size - _pos, 0x7fffffff) : 0; }
  void flush() {}
  void close() { _d.reset(); }
  const char* name() const { return _name.c_str(); }
  bool isDirectory() const { return false; }
  // Arduino Stream search helpers (no timeouts on a file)
  bool findUntil(const char* target, size_t tlen, const char* term, size_t termlen) {
    // Exact: compares the end of what was read with the target and the terminator, so overlapping
    // prefixes ("aab" in "aaab") match as Arduino's findMulti() does (B94).
    std::string seen; int c; size_t keep = std::max(tlen, termlen);
    while ((c = read()) >= 0) {
      seen += (char)c; if (seen.size() > keep) seen.erase(0, seen.size() - keep);
      if (tlen && seen.size() >= tlen && seen.compare(seen.size() - tlen, tlen, target, tlen) == 0) return true;
      if (termlen && seen.size() >= termlen && seen.compare(seen.size() - termlen, termlen, term, termlen) == 0) return false;
    }
    return false;
  }
  bool find(const char* target) { return findUntil(target, strlen(target), "", 0); }
  bool find(char c) { char t[2] = {c, 0}; return find(t); }
  size_t readBytesUntil(char terminator, char* buf, size_t len) {
    size_t n = 0; int c;
    while (n < len && (c = read()) >= 0 && c != terminator) buf[n++] = (char)c;
    return n;
  }
  size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    char b[512]; va_list a; va_start(a, fmt); int n = vsnprintf(b, sizeof b, fmt, a); va_end(a);
    return n > 0 ? write((const uint8_t*)b, std::min<size_t>(n, sizeof b - 1)) : 0;
  }
  size_t printf_P(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    char b[512]; va_list a; va_start(a, fmt); int n = vsnprintf(b, sizeof b, fmt, a); va_end(a);
    return n > 0 ? write((const uint8_t*)b, std::min<size_t>(n, sizeof b - 1)) : 0;
  }
  static inline unsigned writes = 0;   // count of write() calls, for SD-wear style assertions
 private:
  std::shared_ptr<HostFileData> _d;
  bool _writable = false;
  std::string _name;
  uint64_t _pos = 0;
};

class SDClass {
 public:
  File open(const char* path, int mode = FILE_READ) {
    std::string p = norm(path);
    auto it = _files.find(p);
    if (it == _files.end()) {
      if (mode != FILE_WRITE || failCreate) return File();
      it = _files.emplace(p, std::make_shared<HostFileData>()).first;
    }
    return File(it->second, mode == FILE_WRITE, p);
  }
  File open(const String& path, int mode = FILE_READ) { return open(path.c_str(), mode); }
  bool exists(const char* path) { std::string p = norm(path); return _files.count(p) || _dirs.count(p); }
  bool exists(const String& path) { return exists(path.c_str()); }
  bool mkdir(const char* path) { _dirs.insert(norm(path)); return true; }
  bool mkdir(const String& path) { return mkdir(path.c_str()); }
  bool remove(const char* path) { return _files.erase(norm(path)) > 0; }
  bool remove(const String& path) { return remove(path.c_str()); }
  bool rename(const char* from, const char* to) {
    auto it = _files.find(norm(from)); if (it == _files.end()) return false;
    auto d = it->second; _files.erase(it); _files[norm(to)] = d; return true;
  }
  void removePrefix(const char* prefix) {
    for (auto it = _files.begin(); it != _files.end();) it = it->first.rfind(prefix, 0) == 0 ? _files.erase(it) : std::next(it);
  }
  // test helpers
  std::shared_ptr<HostFileData> data(const char* path) { auto it = _files.find(norm(path)); return it == _files.end() ? nullptr : it->second; }
  std::shared_ptr<HostFileData> create(const char* path) { return _files[norm(path)] = std::make_shared<HostFileData>(); }
  void reset() { _files.clear(); _dirs.clear(); failCreate = false; }
  bool failCreate = false;
 private:
  static std::string norm(const char* p) { std::string s = p ? p : ""; return (s.empty() || s[0] != '/') ? "/" + s : s; }
  std::map<std::string, std::shared_ptr<HostFileData>> _files;
  std::set<std::string> _dirs;
};
inline SDClass SD;
inline SDClass& SDFS = SD;                       // the firmware uses both names for the same card
