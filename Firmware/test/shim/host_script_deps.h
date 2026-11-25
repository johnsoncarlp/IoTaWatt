// Host test stand-ins for what IotaScript.cpp uses from the rest of the firmware: the input channel
// table, the integrations ScriptSet, integrators (fake: returns a set value) and simulated solar (fake).
#pragma once
#include "IotaInputChannel.h"
#include "jsonUtil.h"
#include "IotaScript.h"

inline IotaInputChannel** inputChannel = nullptr;
inline int maxInputs = 0;
inline ScriptSet* integrations = nullptr;

struct integrator {
  double value = 0;
  char lastMethod = 0;
  units lastUnits = unitsNone;
  double run(IotaLogRecord*, IotaLogRecord*, units Units, char method) { lastUnits = Units; lastMethod = method; return value; }
};

class simSolar {
 public:
  double e = 0, p = 0;
  double energy(uint32_t, uint32_t) { return e; }
  double power(uint32_t) { return p; }
};
inline simSolar* simsolar = nullptr;
