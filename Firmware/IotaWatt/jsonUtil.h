#ifndef jsonUtil_h
#define jsonUtil_h

#include <ArduinoJson.h>
#include <stdlib.h>
#include <math.h>

/*******************************************************************************************************
 *  ArduinoJson 5 compatible value conversion.
 *
 *  ArduinoJson 5 converted strings when a number or bool was requested: as<int>() on "15" returned 15,
 *  as<bool>() on "true" returned true. ArduinoJson 6 and 7 return 0/false instead.
 *
 *  Config files written by the IoTaWatt web app contain quoted numbers (e.g. "channels":"15"), and
 *  users can edit config.txt by hand, so everything that reads user configuration uses these helpers
 *  to keep the ArduinoJson 5 behaviour:
 *
 *    jInt<T>(v)   integer: string -> v5 parseInteger rules ('t...' -> 1, [+-]digits, stops at non-digit)
 *    jFloat(v)    float:   string -> v5 parseFloat rules ([+-], 't'->1, 'n'->NaN, 'i'->inf, digits[.d][e])
 *    jBool(v)     bool:    jInt<int>(v) != 0  (as in v5: "true" -> true, "false" -> false, 0.5 -> false)
 *    jStr(v)      text:    string as is; number/bool -> its JSON text (v5 as<const char*>() on 12345 gave
 *                          "12345"; v7 gives nullptr). Empty for null/missing/object/array (v5: NULL).
 *                          Returns a String: copy it (charstar) or use it within the same expression.
 *
 *  Missing/null values return 0 / 0.0 / false, as before.
 *
 *  Verified against ArduinoJson 5.13.4 on 39 value cases (host test). String inputs match exactly.
 *  Deliberate differences, where v5 was buggy:
 *    - null as float: v5 NaN, here 0 (and isNull() checks now skip null values -> defaults apply)
 *    - array/object as float: v5 returned garbage (pointer bits), here 0
 *    - bare exponent number (1e3) as integer: v5 1, here 1000
 ******************************************************************************************************/

template <typename T>
T jInt(JsonVariantConst v) {
  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    if (!s) return 0;
    if (*s == 't') return 1;
    bool negative = false;
    if (*s == '-') { negative = true; s++; }
    else if (*s == '+') { s++; }
    T result = 0;
    while (*s >= '0' && *s <= '9') {
      result = T(result * 10 + T(*s - '0'));
      s++;
    }
    return negative ? T(~result + 1) : result;
  }
  if (v.is<bool>()) return v.as<bool>() ? 1 : 0;
  if (v.is<long long>()) return T(v.as<long long>());   // plain cast, as v5 (wraps instead of 0)
  if (v.is<double>()) return T(v.as<double>());         // truncates, as v5
  return 0;
}

inline float jFloat(JsonVariantConst v) {
  if (v.is<const char*>()) {
    // v5 parseFloat grammar: [+-] then 't'->1, 'n'->NaN, 'i'->inf, else digits[.digits][e[+-]digits].
    // No leading whitespace, no hex. Parse only that span.
    const char* s = v.as<const char*>();
    if (!s) return 0;
    bool negative = false;
    if (*s == '-') { negative = true; s++; }
    else if (*s == '+') { s++; }
    if (*s == 't') return 1;
    if (*s == 'n' || *s == 'N') return NAN;
    if (*s == 'i' || *s == 'I') return negative ? -INFINITY : INFINITY;
    char buf[40];
    size_t n = 0;
    bool overflow = false;
    auto put = [&](char c) { if (n < sizeof(buf) - 1) buf[n++] = c; else overflow = true; };
    while (*s >= '0' && *s <= '9') put(*s++);
    if (*s == '.') { put(*s++); while (*s >= '0' && *s <= '9') put(*s++); }
    if (n && (*s == 'e' || *s == 'E')) {
      const char* e = s + 1;
      if (*e == '-' || *e == '+') e++;
      if (*e >= '0' && *e <= '9') {
        put('e'); if (s[1] == '-') put('-');
        s = e; while (*s >= '0' && *s <= '9') put(*s++);
      }
    }
    buf[n] = 0;
    if (overflow) {                       // very long number: let strtof read the original text
      const char* start = v.as<const char*>();
      if (*start == '-' || *start == '+') start++;
      float result = strtof(start, nullptr);
      return negative ? -result : result;
    }
    float result = (n == 0 || (n == 1 && buf[0] == '.')) ? 0 : strtof(buf, nullptr);
    return negative ? -result : result;
  }
  if (v.is<bool>()) return v.as<bool>() ? 1 : 0;
  return v.as<float>();
}

inline bool jBool(JsonVariantConst v) {
  return jInt<int>(v) != 0;
}

template <typename TString>
TString jStrAs(JsonVariantConst v) {
  TString s;
  if (v.is<const char*>()) { s = v.as<const char*>(); return s; }
  if (v.isNull() || v.is<JsonObjectConst>() || v.is<JsonArrayConst>()) return s;
  serializeJson(v, s);                    // number or bool -> its JSON text
  return s;
}

#ifdef ARDUINO
inline String jStr(JsonVariantConst v) { return jStrAs<String>(v); }
#endif

#endif
