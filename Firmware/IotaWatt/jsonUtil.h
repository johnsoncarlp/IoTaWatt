#ifndef jsonUtil_h
#define jsonUtil_h

#include <ArduinoJson.h>
#include <stdlib.h>
#include <math.h>
#include <type_traits>

/*******************************************************************************************************
 *  ArduinoJson 5 compatible value conversion.
 *
 *  ArduinoJson 5 and 7 convert differently (measured on the 39 cases in test/test_jsonutil):
 *    - as<bool>() on a string: v7 is true for ANY non-empty string, so "false" -> true. v5: "false" -> false.
 *    - as<int>() on "15abc", "true", "t", "1.25e2": v7 0. v5 15, 1, 1, 1.
 *    - as<unsigned>() on -1 or "-3": v7 0. v5 wraps (4294967295, 4294967293).
 *    - as<const char*>() on a number or bool: v7 nullptr. v5 its text ("12345").
 *    - "\u0000"-style escapes: v7 decodes them; v5 dropped the backslash ("u0000").
 *  Well-formed numeric strings ("15", "2.5") convert the same in both.
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
    typedef typename std::make_unsigned<T>::type U;     // wrap like v5, but without signed-overflow UB
    U result = 0;
    while (*s >= '0' && *s <= '9') {
      result = U(result * 10u + U(*s - '0'));
      s++;
    }
    return T(negative ? U(~result + 1u) : result);
  }
  if (v.is<bool>()) return v.as<bool>() ? 1 : 0;
  if (v.is<long long>()) return T(v.as<long long>());   // plain cast, as v5 (wraps instead of 0)
  if (v.is<double>()) {                                 // truncates, as v5; casting a non-finite or
    double d = v.as<double>();                          // out-of-range double is UB, so go via long long
    if (!(d > -9.2e18 && d < 9.2e18)) return 0;
    return T((long long)d);
  }
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
