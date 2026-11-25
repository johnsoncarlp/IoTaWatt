// Minimal Arduino.h for host (native) unit tests. Only what the code under test needs;
// add to it as more units come under test. Not used for device builds.
#ifndef ARDUINO_SHIM_H
#define ARDUINO_SHIM_H
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
typedef uint8_t byte;
typedef bool boolean;
#include "host_string.h"                 // ::String, which ArduinoJson uses (ARDUINOJSON_ENABLE_ARDUINO_STRING)
#endif
