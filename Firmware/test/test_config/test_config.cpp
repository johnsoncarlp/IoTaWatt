// setConfig(): config.txt -> device, inputs, outputs, integrations, uploaders, DST, phase tables.
// Real setConfig.cpp, IotaScript, IotaLog and JSON splitter; fakes only for uploaders/PVOutput/integrators.
#include <unity.h>
#include <atomic>
#include <new>
#include <fstream>

// The harness re-creates globals per test without freeing them; exit-time leak reports would be noise.
// test_reload_does_not_leak measures the firmware's own allocations instead.
extern "C" const char* __asan_default_options() { return "detect_leaks=0"; }
static std::atomic<long> liveAllocs{0};
void* operator new(size_t n){ liveAllocs++; void* p = malloc(n ? n : 1); if(!p) throw std::bad_alloc(); return p; }
void* operator new[](size_t n){ liveAllocs++; void* p = malloc(n ? n : 1); if(!p) throw std::bad_alloc(); return p; }
void operator delete(void* p) noexcept { if(p){ liveAllocs--; free(p); } }
void operator delete[](void* p) noexcept { if(p){ liveAllocs--; free(p); } }
void operator delete(void* p, size_t) noexcept { if(p){ liveAllocs--; free(p); } }
void operator delete[](void* p, size_t) noexcept { if(p){ liveAllocs--; free(p); } }
#include <sstream>
#include "host_config_deps.h"
bool setConfig(const char*);
bool recoverConfig();
#include "jsonSplit.cpp"
#include "IotaLog.cpp"
#include "IotaScript.cpp"
#include "setConfig.cpp"
#include "iotaInputChannel.cpp"

static std::string tablesText;
static void putFile(const char* path, const std::string& text){
  auto d = SD.create(path); d->writeAt(0, (const uint8_t*)text.data(), text.size()); d->size = text.size();
}
static std::string getFile(const char* path){
  auto d = SD.data(path); if(!d) return "<missing>";
  std::string s(d->size, 0); d->readAt(0, (uint8_t*)s.data(), d->size); return s;
}

void setUp(void){
  SD.reset(); hostLog.clear(); hostSpiffs.clear(); ESP.restarts = 0;
  for(auto& u : hostUploaders) delete u.second;
  hostUploaders.clear(); hostUploaderAccept = true;
  maxInputs = 0; inputChannel = nullptr;                  // first config after "boot"
  outputs = new ScriptSet(); integrations = new ScriptSet();
  timezoneRule = nullptr; localTimeDiff = 0; PVoutput = nullptr; simsolar = nullptr;
  delete[] deviceName; deviceName = charstar("IotaWatt");
  putFile("/tables.txt", tablesText);
}
void tearDown(void){}

static const char* FULL = R"({
 "format":2, "timezone":"-5", "update":"MAJOR", "logdays":30,
 "device":{"name":"house","version":5,"channels":"15","burden":[0,20,20,20,20,20,20,20,20,20,20,20,20,20,20],
           "httpsproxy":"http://proxy.lan"},
 "dstrule":{"adj":60,"utc":false,"begin":{"month":3,"weekday":1,"instance":2,"time":120},
            "end":{"month":11,"weekday":1,"instance":1,"time":120}},
 "inputs":[
   {"channel":0,"name":"Volts","type":"VT","model":"TDC DA-10-09","cal":9.8,"phase":1.14},
   {"channel":1,"name":"Main1","type":"CT","model":"SCT013-000","turns":2000,"cal":100,"phase":2.7,"vchan":0,"signed":true},
   {"channel":2,"name":"Dryer","type":"CT","model":"AcuCT-H040-50","turns":"1000","phase":1,"vchan":0,"double":true,"reverse":true},
   null,null,null,null,null,null,null,null,null,null,null,null],
 "outputs":[{"name":"Total","units":"Watts","script":"@1+@2"},{"name":"Voltage","units":"Volts","script":"@0"}],
 "integrators":[{"name":"solar","units":"Wh","script":"@1"}],
 "influxdb2":{"type":"influxdb2","url":"http://influx.lan:8086","bucket":"b","orgid":"0123456789abcdef"},
 "pvoutput":{"type":"pvoutput","apikey":"x","systemid":12345,"outputs":[]},
 "simsolar":{"sunrise":360,"sunset":1200,"power":"4000"}
})";

static void test_full_config(void){
  putFile("/config.txt", FULL);
  TEST_ASSERT_TRUE(setConfig("config.txt"));
  TEST_ASSERT_EQUAL_INT(0, ESP.restarts);
  TEST_ASSERT_EQUAL_STRING("house", deviceName);
  TEST_ASSERT_EQUAL_STRING("MAJOR", updateClass);
  TEST_ASSERT_EQUAL_INT32(-300, localTimeDiff);
  TEST_ASSERT_EQUAL_STRING("http://proxy.lan", HTTPSproxy);
  TEST_ASSERT_NOT_NULL(timezoneRule);
  TEST_ASSERT_EQUAL_INT(3, timezoneRule->begPeriod.month); TEST_ASSERT_EQUAL_INT(2, timezoneRule->begPeriod.instance);
  TEST_ASSERT_EQUAL_INT(11, timezoneRule->endPeriod.month); TEST_ASSERT_EQUAL_INT(60, timezoneRule->adjMinutes);
  TEST_ASSERT_EQUAL_UINT32(30, Current_log.setDays(30));
  TEST_ASSERT_EQUAL_INT(15, maxInputs);
  IotaInputChannel* v = inputChannel[0]; IotaInputChannel* c1 = inputChannel[1]; IotaInputChannel* c2 = inputChannel[2];
  TEST_ASSERT_EQUAL_STRING("Volts", v->_name);  TEST_ASSERT_EQUAL_INT(channelTypeVoltage, v->_type);
  TEST_ASSERT_EQUAL_STRING("Main1", c1->_name); TEST_ASSERT_EQUAL_INT(channelTypePower, c1->_type);
  TEST_ASSERT_EQUAL_FLOAT(2000.0f / 20.0f, c1->_calibration);         // turns / burden overrides cal
  TEST_ASSERT_TRUE(c1->_signed);
  TEST_ASSERT_EQUAL_FLOAT(1000.0f / 20.0f, c2->_calibration);         // quoted "1000"
  TEST_ASSERT_TRUE(c2->_double); TEST_ASSERT_TRUE(c2->_reverse);
  TEST_ASSERT_EQUAL_FLOAT(2.0f, c2->_vmult);                          // double: vmult x2
  TEST_ASSERT_FALSE(inputChannel[3]->isActive());
  TEST_ASSERT_EQUAL_INT(2, outputs->count());
  TEST_ASSERT_NOT_NULL(outputs->script("Total"));
  TEST_ASSERT_EQUAL_INT(1, integrations->count());
  TEST_ASSERT_TRUE(hostUploaders.count("influxDB2"));
  TEST_ASSERT_EQUAL_STRING("{\"type\":\"influxdb2\",\"url\":\"http://influx.lan:8086\",\"bucket\":\"b\",\"orgid\":\"0123456789abcdef\"}",
                           hostUploaders["influxDB2"]->lastConfig.c_str());
  TEST_ASSERT_FALSE(hostUploaders.count("Emoncms"));
  TEST_ASSERT_NOT_NULL(PVoutput);
  TEST_ASSERT_TRUE(PVoutput->lastConfig.find("\"systemid\":12345") != std::string::npos);
  TEST_ASSERT_NOT_NULL(simsolar);
  TEST_ASSERT_EQUAL_INT(360, simsolar->sunrise); TEST_ASSERT_EQUAL_UINT32(4000, simsolar->cfgPower);
  TEST_ASSERT_EQUAL_STRING("[0,20,20,20,20,20,20,20,20,20,20,20,20,20,20]", hostSpiffs["/config/device/burden.txt"].c_str());
}

// Phase arrays come from tables.txt; values are stored x100.
static void test_phase_tables(void){
  putFile("/config.txt", FULL);
  TEST_ASSERT_TRUE(setConfig("config.txt"));
  IotaInputChannel* c1 = inputChannel[1];                             // SCT013-000
  TEST_ASSERT_NOT_NULL(c1->_p50); TEST_ASSERT_NOT_NULL(c1->_p60);
  TEST_ASSERT_EQUAL_INT16(310, c1->_p50[0]); TEST_ASSERT_EQUAL_INT16(400, c1->_p50[1]);
  TEST_ASSERT_EQUAL_INT16(270, c1->_p60[0]);
  IotaInputChannel* v = inputChannel[0];                              // TDC DA-10-09: p60 [1.14]
  TEST_ASSERT_NOT_NULL(v->_p60);
  TEST_ASSERT_EQUAL_INT16_MESSAGE(114, v->_p60[0], "TDC DA-10-09 must not get TDC DA-10-09-E6's table");
  TEST_ASSERT_NULL(v->_p50);
}

// getPhase(): pick the phase-correction segment from the table by current (CT) or voltage (VT).
static void test_phase_lookup_by_current(void){
  putFile("/config.txt", FULL);
  TEST_ASSERT_TRUE(setConfig("config.txt"));
  IotaInputChannel* c1 = inputChannel[1];                              // SCT013-000 p60 [2.7,4,2.6,8,2.5,12,...,44,2.1]
  inputChannel[0]->dataBucket.Hz = 60;
  TEST_ASSERT_EQUAL_FLOAT(2.7f, c1->getPhase(1.0f));
  TEST_ASSERT_EQUAL_FLOAT(2.6f, c1->getPhase(5.0f));
  TEST_ASSERT_EQUAL_FLOAT(2.5f, c1->getPhase(8.0f));                   // at a breakpoint: next segment
  TEST_ASSERT_EQUAL_FLOAT(2.1f, c1->getPhase(100.0f));
  TEST_ASSERT_EQUAL_FLOAT(2.1f, c1->getPhase(400.0f));                 // > 327.67 A: still the last segment
  TEST_ASSERT_EQUAL_FLOAT(2.1f, c1->getPhase(5000.0f));
  inputChannel[0]->dataBucket.Hz = 50;
  TEST_ASSERT_EQUAL_FLOAT(3.1f, c1->getPhase(1.0f));                   // 50 Hz table
}

static void test_burden_file_in_spiffs_wins(void){
  hostSpiffs["/config/device/burden.txt"] = "[0,24,10]";
  putFile("/config.txt", FULL);
  TEST_ASSERT_TRUE(setConfig("config.txt"));
  TEST_ASSERT_EQUAL_FLOAT(24.0f, inputChannel[1]->_burden);
  TEST_ASSERT_EQUAL_FLOAT(2000.0f / 24.0f, inputChannel[1]->_calibration);
}

static void test_channel_count_change_restarts(void){
  putFile("/config.txt", FULL);
  TEST_ASSERT_TRUE(setConfig("config.txt"));
  std::string fewer = FULL; fewer.replace(fewer.find("\"channels\":\"15\""), 15, "\"channels\":\"14\"");
  putFile("/config.txt", fewer);
  setConfig("config.txt");
  TEST_ASSERT_EQUAL_INT(0, ESP.restarts);                             // B13 (report only): 14|15 is still 15
}

static void test_comments_accepted(void){
  // Block comments only: JsonSummary strips newlines, so a // comment runs to the end of the summary
  // (the same with ArduinoJson 5; tracker L1).
  std::string c = FULL; c.insert(1, " /* edited by hand */ ");
  c.insert(c.find("\"outputs\""), "/* outputs follow */\n ");
  putFile("/config.txt", c);
  TEST_ASSERT_TRUE(setConfig("config.txt"));
  TEST_ASSERT_EQUAL_INT(2, outputs->count());
}

static void test_unquoted_value_is_rejected_and_kept(void){
  std::string bad = FULL; bad.replace(bad.find("\"MAJOR\""), 7, "MAJOR");
  putFile("/config.txt", bad);
  putFile("/config-1.txt", FULL);
  TEST_ASSERT_FALSE(setConfig("config.txt"));
  TEST_ASSERT_TRUE(recoverConfig());
  TEST_ASSERT_EQUAL_STRING(bad.c_str(), getFile("/config-bad.txt").c_str());   // B14: kept for the user
  TEST_ASSERT_EQUAL_STRING(FULL, getFile("/config.txt").c_str());
}

static void test_out_of_range_values(void){
  std::string c = FULL;
  c.replace(c.find("\"vchan\":0,\"signed\""), 9, "\"vchan\":99");
  c.replace(c.find("\"burden\":"), 9, "\"chanaddr\":[40,1,2],\"chanaref\":[9,9,9],\"burden\":");
  putFile("/config.txt", c);
  TEST_ASSERT_TRUE(setConfig("config.txt"));
  TEST_ASSERT_EQUAL_UINT8(0, inputChannel[1]->_vchannel);             // B26: out of range -> 0, logged
  TEST_ASSERT_EQUAL_UINT8(0, inputChannel[0]->_addr);                 // B30: 40 rejected, default kept
  TEST_ASSERT_EQUAL_UINT8(1, inputChannel[1]->_addr);
  TEST_ASSERT_EQUAL_UINT8(9, inputChannel[2]->_aRef);                 // B7: chanaref applied
  bool vlog = false, alog = false;
  for(auto& l : hostLog){ vlog |= l.find("voltage reference 99") != std::string::npos; alog |= l.find("chanaddr[0] = 40") != std::string::npos; }
  TEST_ASSERT_TRUE(vlog); TEST_ASSERT_TRUE(alog);
}

// "ntp" device setting: trimmed text, blank/missing/wrong type = automatic (nullptr), cleared on reload.
static void test_ntp_setting(void){
  struct { const char* device; const char* expect; } cases[] = {
    {"{\"ntp\":\"ntp.lan\"}", "ntp.lan"},
    {"{\"ntp\":\"  ntp1.lan, 10.0.0.1  \"}", "ntp1.lan, 10.0.0.1"},
    {"{\"ntp\":\"\"}", nullptr}, {"{\"ntp\":\"   \"}", nullptr}, {"{}", nullptr},
    {"{\"ntp\":null}", nullptr}, {"{\"ntp\":[\"a\"]}", nullptr}, {"{\"ntp\":{}}", nullptr},
    {"{\"ntp\":12345}", "12345"},                                      // number: its text (jStr), harmless
  };
  for(auto& c : cases){
    setUp();
    putFile("/config.txt", (std::string("{\"device\":") + c.device + "}").c_str());
    setConfig("config.txt");
    if(c.expect) TEST_ASSERT_EQUAL_STRING_MESSAGE(c.expect, ntpServers, c.device);
    else TEST_ASSERT_NULL_MESSAGE(ntpServers, c.device);
  }
  setUp();                                                             // removing the setting clears it
  putFile("/config.txt", "{\"device\":{\"ntp\":\"ntp.lan\"}}"); setConfig("config.txt");
  putFile("/config.txt", "{\"device\":{\"name\":\"house\"}}"); setConfig("config.txt");
  TEST_ASSERT_NULL(ntpServers);
}

// "ntpinterval" device setting (upstream #368): seconds, 300..86400, default 3600; junk = default.
static void test_ntpinterval_setting(void){
  struct { const char* device; uint32_t expect; } cases[] = {
    {"{}", 3600}, {"{\"ntpinterval\":900}", 900}, {"{\"ntpinterval\":\"600\"}", 600},
    {"{\"ntpinterval\":10}", 300}, {"{\"ntpinterval\":-5}", 300}, {"{\"ntpinterval\":999999}", 86400},
    {"{\"ntpinterval\":0}", 3600}, {"{\"ntpinterval\":\"x\"}", 3600}, {"{\"ntpinterval\":null}", 3600},
    {"{\"ntpinterval\":[1]}", 3600}, {"{\"ntpinterval\":1e9}", 86400},
  };
  for(auto& c : cases){
    setUp(); timeSynchInterval = 1;                                    // must be set either way
    putFile("/config.txt", (std::string("{\"device\":") + c.device + "}").c_str());
    setConfig("config.txt");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(c.expect, timeSynchInterval, c.device);
  }
}

// "sdwebui" device setting: true/"true"/1 turns the SD override on; anything else (or missing) is off.
static void test_sdwebui_setting(void){
  struct { const char* device; bool expect; } cases[] = {
    {"{\"sdwebui\":true}", true}, {"{\"sdwebui\":\"true\"}", true}, {"{\"sdwebui\":1}", true},
    {"{\"sdwebui\":false}", false}, {"{\"sdwebui\":\"false\"}", false}, {"{\"sdwebui\":0}", false},
    {"{}", false}, {"{\"sdwebui\":null}", false}, {"{\"sdwebui\":[1]}", false}, {"{\"sdwebui\":\"yes\"}", false},
  };
  for(auto& c : cases){
    setUp(); webuiFromSD = !c.expect;                                  // must be set either way
    putFile("/config.txt", (std::string("{\"device\":") + c.device + "}").c_str());
    setConfig("config.txt");
    TEST_ASSERT_EQUAL_MESSAGE(c.expect, webuiFromSD, c.device);
  }
}

static void test_minimal_and_wrong_types(void){
  const char* cases[] = {
    "{}",
    "{\"device\":{}}",
    "{\"device\":5,\"inputs\":{\"a\":1},\"outputs\":\"x\",\"integrators\":7,\"dstrule\":[1],\"pvoutput\":\"no\"}",
    "{\"inputs\":[1,\"a\",[2],{\"channel\":\"zero\"}],\"outputs\":[1,null,{}]}",
    "{\"outputs\":[{\"name\":1,\"units\":2,\"script\":3}]}",
  };
  for(const char* c : cases){
    setUp();
    putFile("/config.txt", c);
    setConfig("config.txt");
    TEST_ASSERT_NOT_NULL_MESSAGE(outputs, c);                         // B28: never null
    TEST_ASSERT_EQUAL_INT_MESSAGE(15, maxInputs, c);                  // B42: channels always exist
  }
}

static void test_tables_array_longer_than_100_chars(void){
  // configMasterPhaseArray copies each array into a 100-byte buffer (B44).
  std::string arr; for(int i = 0; i < 40; i++) arr += (i ? "," : "") + std::to_string(1.25 + i);
  std::string t = "{\"version\":\"9.9.9\",\"CT\":[{\"model\":\"Long\",\"p60\":[" + arr + "]}]}";
  putFile("/tables.txt", t);
  std::string c = FULL; c.replace(c.find("\"SCT013-000\""), 12, "\"Long\"");
  putFile("/config.txt", c);
  TEST_ASSERT_TRUE(setConfig("config.txt"));
  TEST_ASSERT_NOT_NULL(inputChannel[1]->_p60);
}

// Saving a config in the web UI reloads it; on an 80 KB device a leak per reload adds up.
static void test_reload_does_not_leak(void){
  putFile("/config.txt", FULL);
  TEST_ASSERT_TRUE(setConfig("config.txt"));
  TEST_ASSERT_TRUE(setConfig("config.txt"));                     // warm up: first-time allocations
  hostLog.clear(); hostLog.reserve(64);                          // the harness keeps log lines: not a leak
  long before = liveAllocs.load();
  for(int i = 0; i < 50; i++){ TEST_ASSERT_TRUE(setConfig("config.txt")); hostLog.clear(); }
  long grown = liveAllocs.load() - before;
  printf("live allocations after 50 reloads: %+ld\n", grown);
  TEST_ASSERT_EQUAL_INT32_MESSAGE(0, (int32_t)grown, "allocations leaked per config reload");
}

static void test_fuzz_mutated_config(void){
  uint32_t seed = 8080;
  auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
  const char junk[] = "{}[]\",:0123456789-.ae \n/*";
  for(int iter = 0; iter < 1500; iter++){
    setUp();
    std::string c = FULL;
    int edits = 1 + rnd() % 6;
    for(int e = 0; e < edits; e++){
      size_t at = rnd() % c.size();
      switch(rnd() % 3){
        case 0: c.erase(at, 1 + rnd() % 8); break;
        case 1: c.insert(at, 1, junk[rnd() % (sizeof(junk) - 1)]); break;
        default: c[at] = junk[rnd() % (sizeof(junk) - 1)];
      }
    }
    putFile("/config.txt", c);
    setConfig("config.txt");
    TEST_ASSERT_NOT_NULL(outputs);
  }
}

// B94: the SD stand-in's File::find() matches as Arduino's findMulti() does, overlapping prefixes too
// (the tables lookup by model depends on it).
static void test_stand_in_find(void){
  auto open = [](const char* s){ auto f = SD.create("/find.txt"); f->writeAt(0, (const uint8_t*)s, strlen(s)); f->size = strlen(s); return SD.open("/find.txt"); };
  File f = open("xxaaabyy"); TEST_ASSERT_TRUE(f.find("aab")); f.close();
  f = open("abababc");      TEST_ASSERT_TRUE(f.find("ababc")); f.close();
  f = open("aaaa");         TEST_ASSERT_FALSE(f.find("aab")); f.close();
  f = open("aXbb");         TEST_ASSERT_FALSE(f.findUntil("bb", 2, "X", 1)); f.close();
  SD.remove("/find.txt");
}

int main(int, char**){
  for(const char* p : {"../SD/tables.txt", "SD/tables.txt", "../../SD/tables.txt"}){
    std::ifstream f(p); if(f){ std::stringstream ss; ss << f.rdbuf(); tablesText = ss.str(); break; }
  }
  if(tablesText.empty()){ printf("tables.txt not found\n"); return 1; }
  UNITY_BEGIN();
  RUN_TEST(test_stand_in_find);
  RUN_TEST(test_full_config);
  RUN_TEST(test_phase_tables);
  RUN_TEST(test_phase_lookup_by_current);
  RUN_TEST(test_burden_file_in_spiffs_wins);
  RUN_TEST(test_channel_count_change_restarts);
  RUN_TEST(test_comments_accepted);
  RUN_TEST(test_unquoted_value_is_rejected_and_kept);
  RUN_TEST(test_out_of_range_values);
  RUN_TEST(test_ntp_setting);
  RUN_TEST(test_ntpinterval_setting);
  RUN_TEST(test_sdwebui_setting);
  RUN_TEST(test_minimal_and_wrong_types);
  RUN_TEST(test_tables_array_longer_than_100_chars);
  RUN_TEST(test_reload_does_not_leak);
  RUN_TEST(test_fuzz_mutated_config);
  return UNITY_END();
}
