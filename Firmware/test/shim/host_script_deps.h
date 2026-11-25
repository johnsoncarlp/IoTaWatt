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
  template <class R> void newLogEntry(R*, R*) {}      // dataLog's hook (integrator.cpp isn't under test)
  double value = 0;
  char lastMethod = 0;
  units lastUnits = unitsNone;
  bool ended = false;
  double run(IotaLogRecord*, IotaLogRecord*, units Units, char method) { lastUnits = Units; lastMethod = method; return value; }
  bool config(Script*) { return true; }
  void end() { ended = true; }
  IotaLog* log = nullptr;                      // set by a test to report an integration log
  IotaLog* get_log() { return log; }
};

class simSolar {
 public:
  double e = 0, p = 0;
  int sunrise = 0, sunset = 0; uint32_t cfgPower = 0;
  bool config(int r, int s, uint32_t pw) { sunrise = r; sunset = s; cfgPower = pw; return true; }
  double energy(uint32_t, uint32_t) { return e; }
  double power(uint32_t) { return p; }
};
inline simSolar* simsolar = nullptr;
