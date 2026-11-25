// Host test stand-ins for release-file handling (releaseFile.cpp): the signing public key (set by the
// test), MD5Builder (fake: the device's ROM MD5 isn't available; returns 32 '0's), deleteRecursive on the
// in-memory SD, and the real Crypto library's SHA256/Ed25519.
#pragma once
#include "host_iotawatt.h"
#include <SHA256.h>
#include <Ed25519.h>

inline uint8_t publicKey[32];

class MD5Builder {
 public:
  void begin() {}
  void add(const uint8_t*, uint16_t) {}
  void calculate() {}
  void getChars(char* out) { memset(out, '0', 32); }
};

inline void deleteRecursive(String path) {
  // The stand-in filesystem is flat: remove the file and anything below "path/".
  SD.remove(path.c_str());
  std::string p = path.str(); if (p.empty() || p[0] != '/') p = "/" + p;
  SD.removePrefix((p + "/").c_str());
}
