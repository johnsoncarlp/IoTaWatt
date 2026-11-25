// Host test stand-in for libb64's decoder (standard base64 alphabet, as the browser's btoa()).
#pragma once
#include <cstring>
#include <cstdint>
inline int base64_decode_chars(const char* in, int len, char* out) {
  auto v = [](char c) -> int { if (c >= 'A' && c <= 'Z') return c - 'A'; if (c >= 'a' && c <= 'z') return c - 'a' + 26;
                               if (c >= '0' && c <= '9') return c - '0' + 52; if (c == '+') return 62; if (c == '/') return 63; return -1; };
  int n = 0, bits = 0; uint32_t acc = 0;
  for (int i = 0; i < len; i++) { int d = v(in[i]); if (d < 0) continue; acc = ((acc << 6) | (uint32_t)d) & 0xFFFFFF; bits += 6;
    if (bits >= 8) { bits -= 8; out[n++] = (char)((acc >> bits) & 0xff); } }
  return n;
}
