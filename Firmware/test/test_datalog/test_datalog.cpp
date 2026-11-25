// dataLog() (the 5-second current log) through a simulated scheduler, including the clock stepping
// backwards: after a restart that reads an RTC behind the log, and while running (an NTP correction).
// Tracker B57.
#include <unity.h>
#include "host_datalog_deps.h"
#include "IotaLog.cpp"
#include "IotaScript.cpp"
#include "timeRules.cpp"
#include "utilities.cpp"
#include "splitstr.cpp"
#include "iotaInputChannel.cpp"
#include "dataLog.cpp"

static IotaInputChannel* chans[2];
static uint32_t T0;
static uint64_t simMs = 0;                       // the device's millis(); UTC = utcBase + simMs/1000
static int64_t  utcBase = 0;
static void syncClock(){ hostMillis = (uint32_t)simMs; hostUTC = (uint32_t)(utcBase + (int64_t)(simMs / 1000)); }
static void setUTC(uint32_t t){ utcBase = (int64_t)t - (int64_t)(simMs / 1000); syncClock(); }   // a clock step

struct Run { uint32_t calls = 0; uint32_t maxWdtGap = 0; };
// Run the service as the scheduler would until UTC reaches `until`: 1 = now, <=1000 = that many ms,
// else a UTC time. Tracks the longest stretch (in simulated seconds) with the watchdog not re-armed.
static Run runUntil(serviceBlock* sb, uint32_t until, uint32_t callCap = 200000){
  Run r; uint32_t lastArm = hostUTC;
  while(hostUTC < until && r.calls < callCap){
    size_t arms = hostLogWDT ? hostLogWDT->armedAt.size() : 0;
    uint32_t t = sb->service(sb); r.calls++;
    if(hostLogWDT && hostLogWDT->armedAt.size() != arms) lastArm = hostUTC;
    if(hostLogWDT && hostLogWDT->armed) r.maxWdtGap = std::max(r.maxWdtGap, hostUTC - lastArm);
    if(t == 0) break;
    if(t == 1) simMs += 1;
    else if(t <= 1000) simMs += t;
    else if(t > hostUTC) simMs += (uint64_t)(t - hostUTC) * 1000;
    else simMs += 1;
    syncClock();
    if(hostLogWDT && hostLogWDT->armed) r.maxWdtGap = std::max(r.maxWdtGap, hostUTC - lastArm);
  }
  return r;
}
static bool logged(const char* s){ for(auto& l : hostLog) if(l.find(s) != std::string::npos) return true; return false; }

void setUp(void){}
void tearDown(void){}

static serviceBlock* sb;

// Restart with the RTC 41 minutes behind the last log entry (24Nov2026). Logging can't write keys
// before the last one, so it waits for the clock, but cheaply, with its watchdog kept fed, and says so.
static void test_restart_with_clock_behind_log(void){
  uint32_t last = T0 + 3600;
  { IotaLogRecord r; for(uint32_t t = T0; t <= last; t += 5){ r.UNIXtime = t; r.logHours = (t - T0) / 3600.0; Current_log.write(&r); } }
  Current_log.end();
  setUTC(last - 2460);
  hostLog.clear();
  Run r = runUntil(sb, last + 60);
  TEST_ASSERT_TRUE_MESSAGE(hostUTC >= last + 60, "logging never caught up");
  TEST_ASSERT_LESS_THAN_UINT32(2000, r.calls);                 // ~5 s polls, not a 2 ms spin
  TEST_ASSERT_LESS_THAN_UINT32(300, r.maxWdtGap);              // the 300 s watchdog would not fire
  TEST_ASSERT_TRUE(logged("behind the last log entry"));
  TEST_ASSERT_TRUE(Current_log.lastKey() > last);              // logging resumed after the last entry
  TEST_ASSERT_EQUAL_UINT32(0, Current_log.lastKey() % 5);
}

// While running: the clock steps back 46 minutes (an NTP correction of a fast clock).
static void test_clock_steps_back_while_running(void){
  runUntil(sb, hostUTC + 600);                                  // normal logging for 10 minutes
  uint32_t last = Current_log.lastKey();
  TEST_ASSERT_TRUE(last + 10 >= hostUTC);
  hostLog.clear();
  setUTC(hostUTC - 2760);
  Run r = runUntil(sb, last + 60);
  TEST_ASSERT_TRUE_MESSAGE(hostUTC >= last + 60, "logging never caught up");
  TEST_ASSERT_LESS_THAN_UINT32(2000, r.calls);
  TEST_ASSERT_LESS_THAN_UINT32(300, r.maxWdtGap);
  TEST_ASSERT_TRUE(logged("behind the last log entry"));
  TEST_ASSERT_TRUE(Current_log.lastKey() > last);
}

// A forward step beyond the gap-fill limit skips ahead (unchanged behaviour); small ones fill in.
static void test_clock_steps_forward(void){
  runUntil(sb, hostUTC + 60);
  hostLog.clear();
  setUTC(hostUTC + 3 * 3600);
  runUntil(sb, hostUTC + 30);
  TEST_ASSERT_TRUE(Current_log.lastKey() + 10 >= hostUTC);
  TEST_ASSERT_FALSE(logged("behind the last log entry"));
}

int main(int, char**){
  setenv("TZ", "UTC", 1); tzset();
  struct tm t{}; t.tm_year = 2026 - 1900; t.tm_mon = 10; t.tm_mday = 24; T0 = (uint32_t)timegm(&t);
  for(int i = 0; i < 2; i++){ chans[i] = new IotaInputChannel(i); chans[i]->_vmult = 1; chans[i]->active(true); }
  chans[0]->_type = channelTypeVoltage; chans[1]->_type = channelTypePower;
  inputChannel = chans; maxInputs = 2;
  integrations = new ScriptSet();
  SD.reset(); Current_log.begin(IOTA_CURRENT_LOG_PATH);
  simMs = 10000; setUTC(T0);
  sb = new serviceBlock(); sb->service = dataLog;
  UNITY_BEGIN();
  RUN_TEST(test_restart_with_clock_behind_log);
  RUN_TEST(test_clock_steps_back_while_running);
  RUN_TEST(test_clock_steps_forward);
  return UNITY_END();
}
