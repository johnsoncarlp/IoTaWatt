// PVoutput uploader (not Uploader based): configuration, lifecycle and the PVoutput global, driven
// through its real state machine with the recording fake HTTP client.
#include <unity.h>
#include <new>
#include "host_uploader_deps.h"
extern "C" const char* __asan_default_options() { return "detect_leaks=0"; }
#include "IotaLog.cpp"
#include "IotaScript.cpp"
#include "timeRules.cpp"
#include "utilities.cpp"
#include "xurl.cpp"
#include "splitstr.cpp"
#include "uploaders/PVoutput_uploader.cpp"
int hostDeleteUploaderCalls = 0;
bool delete_uploader(const char*){ hostDeleteUploaderCalls++; return false; }   // registry: PVoutput isn't in it

static uint32_t T0;
static IotaInputChannel* chans[2];
static const char* CFG = "{\"type\":\"pvoutput\",\"revision\":1,\"apikey\":\"0123456789abcdef0123456789abcdef01234567\","
                         "\"systemid\":\"12345\",\"interval\":5,\"reload\":false,"
                         "\"outputs\":[{\"name\":\"generation\",\"units\":\"Watts\",\"script\":\"@1\"}]}";

void setUp(void){ hostHttpLog.clear(); hostHttpReplies.clear(); hostServices.clear(); hostLog.clear(); hostUTC = T0 + 2 * 3600; WiFi.connected = true; }
void tearDown(void){}

// B54: end() before the service ever ran deletes the instance at once; the global must not dangle.
static void test_end_unstarted_clears_global(void){
  PVoutput = new PVoutput_uploader();
  PVoutput->end();
  TEST_ASSERT_NULL(PVoutput);
}

// Run the service, letting scheduled times pass (the fake scheduler runs it back to back).
static void runSteps(serviceBlock* sb, int n){ for(int i = 0; i < n; i++){ uint32_t t = sb->service(sb); if(t > hostUTC) hostUTC = t; } }

// B54: end() while running deletes the instance when the state machine stops; the global must not dangle.
// The server answers getsystem and getstatus, so the service reaches its upload loop.
static void test_end_running_clears_global(void){
  PVoutput = new PVoutput_uploader();
  TEST_ASSERT_TRUE(PVoutput->config(CFG));
  serviceBlock* sb = hostServices.back();
  hostHttpReplies.push_back({200, "PVOutput System,1000,,,,,,,,,,,,,,5;;1"});
  hostHttpReplies.push_back({200, "20261123,02:00,100,100"});
  runSteps(sb, 20);
  TEST_ASSERT_TRUE(hostHttpLog.size() >= 2);
  PVoutput->end();
  for(int i = 0; i < 200 && PVoutput; i++) runSteps(sb, 1);
  TEST_ASSERT_NULL(PVoutput);
  delete sb;
}

// B55: end() while the server can't be reached (bad key, no internet) must still stop the service.
static void test_end_while_server_unreachable(void){
  PVoutput = new PVoutput_uploader();
  TEST_ASSERT_TRUE(PVoutput->config(CFG));
  serviceBlock* sb = hostServices.back();
  runSteps(sb, 20);                                          // no replies: getsystem keeps failing
  PVoutput->end();
  for(int i = 0; i < 200 && PVoutput; i++) runSteps(sb, 1);
  TEST_ASSERT_NULL(PVoutput);
  delete sb;
}

// B55: removed then re-added before the old instance stopped: it must keep running, not end itself.
static void test_readd_before_stop_keeps_running(void){
  PVoutput = new PVoutput_uploader();
  TEST_ASSERT_TRUE(PVoutput->config(CFG));
  serviceBlock* sb = hostServices.back();
  runSteps(sb, 5);
  PVoutput->end();                                           // config saved without pvoutput
  PVoutput_uploader* same = PVoutput;
  TEST_ASSERT_TRUE(PVoutput->config(CFG));                   // ... and saved again with it
  hostHttpReplies.push_back({200, "PVOutput System,1000,,,,,,,,,,,,,,5;;1"});
  hostHttpReplies.push_back({200, "20261123,02:00,100,100"});
  for(int i = 0; i < 20; i++) hostHttpReplies.push_back({200, "1"});
  runSteps(sb, 300);
  TEST_ASSERT_EQUAL_PTR(same, PVoutput);                     // still the configured instance
  delete PVoutput;
  delete sb;
}

// B64: "stop" set at boot (the Stop button, then a restart), then Start: the service must initialise and
// talk to PVOutput, not loop on the upload step with no interval. Before the fix it never sent a request
// and ran back to back.
static void test_start_after_stopped_at_boot(void){
  std::string stopped = std::string(CFG); stopped.insert(stopped.size() - 1, ",\"stop\":true");
  PVoutput = new PVoutput_uploader();
  TEST_ASSERT_TRUE(PVoutput->config(stopped.c_str()));
  serviceBlock* sb = hostServices.back();
  runSteps(sb, 20);
  TEST_ASSERT_EQUAL_INT(0, (int)hostHttpLog.size());          // stopped: nothing sent
  std::string started = std::string(CFG); started.replace(started.find("\"revision\":1"), 12, "\"revision\":2");
  TEST_ASSERT_TRUE(PVoutput->config(started.c_str()));        // Start (the web UI saves a new revision)
  hostHttpReplies.push_back({200, "PVOutput System,1000,,,,,,,,,,,,,,5;;1"});
  hostHttpReplies.push_back({200, "20261123,02:00,100,100"});
  for(int i = 0; i < 20; i++) hostHttpReplies.push_back({200, "1"});
  uint32_t start = hostUTC;
  runSteps(sb, 300);
  TEST_ASSERT_TRUE_MESSAGE(hostHttpLog.size() >= 2, "Start must reach getsystem/getstatus");
  TEST_ASSERT_TRUE_MESSAGE(hostUTC > start + 60, "the service must wait between uploads, not run back to back");
  delete PVoutput;
  delete sb;
}

// Bad configurations are rejected without crashing (each a fresh instance).
static void test_bad_configs(void){
  const char* bad[] = {"", "{", "[]", "{\"apikey\":5}", "{\"systemid\":\"1\"}",
    "{\"apikey\":\"k\",\"systemid\":\"1\",\"interval\":\"x\",\"outputs\":7}",
    "{\"apikey\":\"k\",\"systemid\":\"1\",\"outputs\":[{\"name\":\"voltage\",\"script\":\"@1+\"}]}", nullptr};
  for(int i = 0; bad[i]; i++){
    PVoutput_uploader* p = new PVoutput_uploader();
    p->config(bad[i]);
    for(auto* b : hostServices) delete b;
    hostServices.clear();
    delete p;
  }
}

int main(int, char**){
  setenv("TZ", "UTC", 1); tzset();
  struct tm t{}; t.tm_year = 2026 - 1900; t.tm_mon = 10; t.tm_mday = 23; T0 = (uint32_t)timegm(&t);
  for(int i = 0; i < 2; i++){ chans[i] = new IotaInputChannel(i); chans[i]->_vmult = 1; chans[i]->active(true); }
  chans[0]->_type = channelTypeVoltage; chans[1]->_type = channelTypePower;
  inputChannel = chans; maxInputs = 2;
  SD.reset(); Current_log.begin("/iotawatt/iotalog.log"); History_log.begin("/iotawatt/histlog.log");
  IotaLogRecord r; for(uint32_t ts = T0; ts <= T0 + 3 * 3600; ts += 5){ r.UNIXtime = ts; r.logHours = (ts - T0) / 3600.0; Current_log.write(&r);
    if(ts % 60 == 0) History_log.write(&r); }
  UNITY_BEGIN();
  RUN_TEST(test_end_unstarted_clears_global);
  RUN_TEST(test_end_running_clears_global);
  RUN_TEST(test_end_while_server_unreachable);
  RUN_TEST(test_readd_before_stop_keeps_running);
  RUN_TEST(test_start_after_stopped_at_boot);
  RUN_TEST(test_bad_configs);
  return UNITY_END();
}
