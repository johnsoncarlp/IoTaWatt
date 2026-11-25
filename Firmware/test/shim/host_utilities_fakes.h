// Simple versions of utilities.cpp helpers for suites that don't compile the real utilities.cpp.
// A suite that does defines IOTAWATT_HOST_REAL_UTILITIES first.
#pragma once
#ifndef IOTAWATT_HOST_REAL_UTILITIES
#include <cctype>
#include <cstring>
#include "host_string.h"
inline char* charstar(const char* str1, const char* str2 = nullptr) {
  size_t l1 = str1 ? strlen(str1) : 0, l2 = str2 ? strlen(str2) : 0;
  if (!(l1 + l2)) return nullptr;
  char* p = new char[l1 + l2 + 1];
  if (str1) strcpy(p, str1); else p[0] = 0;
  if (str2) strcpy(p + l1, str2);
  return p;
}
inline char* charstar(const String& s) { return charstar(s.c_str()); }
inline int strcmp_ci(const char* str1, const char* str2) {        // same algorithm as utilities.cpp
  const char* char1 = str1; const char* char2 = str2;
  while (*char1 || *char2) {
    if (*(char1++) != *(char2++)) {
      if (toupper(*(char1 - 1)) > toupper(*(char2 - 1))) return +1;
      if (toupper(*(char1 - 1)) < toupper(*(char2 - 1))) return -1;
    }
  }
  return 0;
}
#endif
