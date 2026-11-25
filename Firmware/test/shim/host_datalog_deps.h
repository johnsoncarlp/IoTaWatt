// Host test stand-ins for dataLog.cpp: a recording Ticker (the datalog watchdog), the history log hook,
// the run flags, and the log path. Builds on the uploader stand-ins (IotaLog, scheduler, UTCtime).
#pragma once
#include "host_uploader_deps.h"
#include "host_channel_deps.h"
#ifndef MAXINPUTS
#define MAXINPUTS 15                             // as IotaWatt.h
#endif
#define MS_PER_HOUR 3600000UL
#ifndef IOTA_CURRENT_LOG_PATH
#define IOTA_CURRENT_LOG_PATH "/iotawatt/iotalog.log"
#define IOTA_HISTORY_LOG_PATH "/iotawatt/histlog.log"
#endif
struct Ticker {                                  // records arming; tests decide when it would have fired
  bool armed = false; float seconds = 0; void (*fn)() = nullptr; std::vector<uint32_t> armedAt;
  void attach(float s, void (*f)()) { armed = true; seconds = s; fn = f; armedAt.push_back(UTCtime()); }
  void detach() { armed = false; }
};
inline Ticker* hostLogWDT = nullptr;             // set by dataLog's attach (see dataLog.cpp host hook)
inline bool     RTCrunning = true, sampling = true;
inline uint32_t HTTPlock = 0;
inline int      hostDropDead = 0;
inline void     dropDead() { hostDropDead++; }
inline std::vector<uint32_t> hostHistoryCalls;
inline void     logtoHistory(IotaLogRecord* r) { hostHistoryCalls.push_back(r->UNIXtime); }
