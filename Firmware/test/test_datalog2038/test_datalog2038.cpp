// dataLog() started on a new, empty log after January 2038 (UTC past INT32_MAX). Its own program:
// dataLog's state machine initialises once, as at boot. Tracker B67 (same harness as test_datalog).
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

// Logging starts at the current time, without a "clock behind" message, instead of filling in from 1970.
static void test_new_log_after_2038(void){
  hostLog.clear();
  runUntil(sb, hostUTC + 30, 20000);
  TEST_ASSERT_TRUE(logged("New current log created"));
  TEST_ASSERT_FALSE(logged("behind the last log entry"));
  TEST_ASSERT_TRUE_MESSAGE(Current_log.lastKey() + 10 >= hostUTC, "logging must start at the current time");
  TEST_ASSERT_TRUE(Current_log.firstKey() + 60 >= hostUTC);
}

int main(int, char**){
  setenv("TZ", "UTC", 1); tzset();
  struct tm t{}; t.tm_year = 2039 - 1900; t.tm_mon = 5; t.tm_mday = 1; T0 = (uint32_t)timegm(&t);
  for(int i = 0; i < 2; i++){ chans[i] = new IotaInputChannel(i); chans[i]->_vmult = 1; chans[i]->active(true); }
  chans[0]->_type = channelTypeVoltage; chans[1]->_type = channelTypePower;
  inputChannel = chans; maxInputs = 2;
  integrations = new ScriptSet();
  SD.reset();
  simMs = 10000; setUTC(T0);
  sb = new serviceBlock(); sb->service = dataLog;
  UNITY_BEGIN();
  RUN_TEST(test_new_log_after_2038);
  return UNITY_END();
}
