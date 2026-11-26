// Web server handlers (webServer.cpp, graphSave.cpp) against a fake ESP8266WebServer that records every
// response: /status, /vcal, /command, passwords, feed list, graphs, directory listing and /query.
#include <unity.h>
#define IOTAWATT_HOST_WEB                     // CSVquery uses the web stand-ins (one fake server)
#include "host_web_deps.h"
#include "IotaLog.cpp"
#include "IotaScript.cpp"
#include "timeRules.cpp"
#include "utilities.cpp"
#include "xurl.cpp"
#include "splitstr.cpp"
#include "iotaInputChannel.cpp"
#include "uploaders/Uploader.cpp"
#include "uploaders/influxBase.cpp"
#include "uploaders/influxDB_uploader.cpp"
#include "CSVquery.cpp"
#include "webGuard.cpp"
#include "webServer.cpp"
#include "graphSave.cpp"
#include "webui.cpp"
#include <zlib.h>
#include <fstream>
#include <sstream>
static bool hostAdminResult = true;                     // auth.cpp isn't under test here: logged in, at
bool auth(authLevel l) { return l == authAdmin ? hostAuthResult && hostAdminResult : hostAuthResult; }   // which level

static uint32_t T0;
static IotaInputChannel* chans[3];

static JsonDocument lastJson(int expectCode = 200){
  TEST_ASSERT_FALSE(server.responses.empty());
  const HostResponse& r = server.responses.back();
  TEST_ASSERT_EQUAL_INT_MESSAGE(expectCode, r.code, r.body.c_str());
  JsonDocument d; TEST_ASSERT_FALSE_MESSAGE(deserializeJson(d, r.body), r.body.c_str());
  return d;
}
static void request(std::initializer_list<std::pair<const char*, std::string>> a){
  server.reset(); for(auto& kv : a) server.argv[kv.first] = kv.second;
}
static std::string b64(const std::string& s){            // standard base64 (as the browser's btoa)
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o; uint32_t v = 0; int bits = -6;
  for(unsigned char c : s){ v = ((v << 8) | c) & 0xFFFFFF; bits += 8; while(bits >= 0){ o += t[(v >> bits) & 63]; bits -= 6; } }
  if(bits > -6) o += t[((v << 8) >> (bits + 8)) & 63];
  while(o.size() % 4) o += '=';
  return o;
}

void setUp(void){
  server.reset(); hostLog.clear(); ESP.restarts = 0; WiFi.disconnects = 0; hostSavedPwds = 0;
  delete[] adminH1; adminH1 = nullptr; delete[] userH1; userH1 = nullptr; localAccess = false;
}
void tearDown(void){}

static void test_status_stats_wifi_passwords_device(void){
  samplesPerCycle = 643.25f; cycleSampleRate = 39.75f; frequency = 59.96f; programStartTime = hostUTC - 4185;
  wifiConnectTime = hostUTC - 100; deviceName = (char*)"house";
  request({{"stats", ""}, {"wifi", ""}, {"passwords", ""}, {"device", ""}});
  handleStatus();
  JsonDocument d = lastJson();
  TEST_ASSERT_FLOAT_WITHIN(0.001, 643.25f, d["stats"]["cyclerate"].as<float>());
  TEST_ASSERT_EQUAL_STRING("IoTaWatt Reimagined", d["stats"]["product"].as<const char*>());
  TEST_ASSERT_EQUAL_UINT32(4185, d["stats"]["runseconds"].as<uint32_t>());
  TEST_ASSERT_EQUAL_STRING(IOTAWATT_VERSION, d["stats"]["version"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("TestNet", d["wifi"]["SSID"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("192.168.1.50", d["wifi"]["IP"].as<const char*>());
  TEST_ASSERT_EQUAL_INT(-61, d["wifi"]["RSSI"].as<int>());
  TEST_ASSERT_FALSE(d["passwords"]["admin"].as<bool>());
  TEST_ASSERT_EQUAL_STRING("house", d["device"]["name"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("NONE", d["device"]["update"].as<const char*>());
}

static void test_status_inputs_and_outputs(void){
  statRecord = IotaLogRecord(); statRecord.accum1[0] = 120.5; statRecord.accum2[0] = 60;
  statRecord.accum1[1] = 900; statRecord.accum2[1] = 1000;            // 900 W, 1000 VA: PF 0.9
  statRecord.accum1[2] = 1.4;                                          // |W| < 2: reported as 0
  request({{"inputs", ""}, {"outputs", ""}});
  handleStatus();
  JsonDocument d = lastJson();
  JsonArray in = d["inputs"];
  TEST_ASSERT_EQUAL_INT(3, in.size());
  TEST_ASSERT_FLOAT_WITHIN(0.01, 120.5, in[0]["Vrms"].as<float>());
  TEST_ASSERT_EQUAL_STRING("900", in[1]["Watts"].as<const char*>());   // Watts is a string in the API
  TEST_ASSERT_FLOAT_WITHIN(0.0001, 0.9, in[1]["Pf"].as<float>());
  TEST_ASSERT_EQUAL_STRING("0", in[2]["Watts"].as<const char*>());
  JsonArray out = d["outputs"];
  TEST_ASSERT_EQUAL_STRING("Total", out[0]["name"].as<const char*>());
  TEST_ASSERT_FLOAT_WITHIN(0.01, 900.0 + 0.0, out[0]["value"].as<float>());   // @1+@2 with @2 = 0 (statRecord accum1[2] zeroed)
}

static void test_status_uploaders_and_datalogs(void){
  influxDB_uploader* u = new influxDB_uploader;
  TEST_ASSERT_TRUE(u->config("{\"revision\":1,\"url\":\"http://x:8086\",\"database\":\"d\",\"postInterval\":10,"
                             "\"outputs\":[{\"name\":\"Main\",\"units\":\"Watts\",\"script\":\"@1\"}]}"));
  hostWebUploaders = {u};
  PVoutput_uploader pv; PVoutput = &pv;
  JsonDocument id; deserializeJson(id, "[{\"name\":\"solar\",\"units\":\"Wh\",\"script\":\"@1\"},{\"name\":\"nolog\",\"units\":\"Wh\",\"script\":\"@2\"}]");
  delete integrations; integrations = new ScriptSet(id.as<JsonArray>());
  integrator withLog, noLog; withLog.log = &Current_log;
  integrations->first()->setParm(&withLog); integrations->first()->next()->setParm(&noLog);
  request({{"uploaders", ""}, {"datalogs", ""}});
  handleStatus();
  JsonDocument d = lastJson();
  TEST_ASSERT_EQUAL_STRING("influxDB", d["uploaders"][0]["id"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("PVoutput", d["uploaders"][1]["id"].as<const char*>());
  JsonArray logs = d["datalogs"];
  TEST_ASSERT_EQUAL_STRING("Current", logs[0]["id"].as<const char*>());
  TEST_ASSERT_EQUAL_UINT32(Current_log.lastKey(), logs[0]["lastkey"].as<uint32_t>());
  TEST_ASSERT_EQUAL_INT(3, logs.size());                               // integration without an open log skipped (B5)
  TEST_ASSERT_EQUAL_STRING("solar", logs[2]["id"].as<const char*>());
  hostWebUploaders.clear(); PVoutput = nullptr; delete u;
  for(auto* b : hostServices) delete b;                     // the device's scheduler owns these
  hostServices.clear();
  delete integrations; integrations = new ScriptSet();
}

static void test_vcal(void){
  request({{"channel", "0"}}); handleVcal();
  TEST_ASSERT_EQUAL_INT(400, server.responses.back().code);            // missing cal
  request({{"channel", "9"}, {"cal", "10"}}); handleVcal();
  TEST_ASSERT_EQUAL_INT(400, server.responses.back().code);            // B26: out of range
  request({{"channel", "0"}, {"cal", "10.5"}}); handleVcal();
  JsonDocument d = lastJson();
  TEST_ASSERT_FLOAT_WITHIN(0.01, 121.5, d["vrms"].as<float>());
}

static void test_command(void){
  request({{"vtphase", "1"}, {"refchan", "0"}}); handleCommand();
  TEST_ASSERT_EQUAL_STRING("Phase Shift chan 1 ref 0, 1.25", server.responses.back().body.c_str());
  request({{"vtphase", "7"}}); handleCommand();
  TEST_ASSERT_EQUAL_INT(400, server.responses.back().code);
  request({{"vtphase", "1"}, {"refchan", "-1"}}); handleCommand();
  TEST_ASSERT_EQUAL_INT(400, server.responses.back().code);
  request({{"sample", "5"}}); handleCommand();
  TEST_ASSERT_EQUAL_INT(400, server.responses.back().code);
  hostSampleCycleRc = 3; request({{"sample", "1"}}); handleCommand();
  TEST_ASSERT_EQUAL_STRING("Sample Channel 1, return code 3", server.responses.back().body.c_str());
  hostSampleCycleRc = 0; request({{"sample", "1"}}); handleCommand();
  TEST_ASSERT_EQUAL_STRING("samples", server.responses.back().body.c_str());
  request({{"restart", ""}}); handleCommand();
  TEST_ASSERT_EQUAL_STRING("ok", server.responses.front().body.c_str());
  TEST_ASSERT_EQUAL_INT(1, ESP.restarts);
  request({{"disconnect", ""}}); handleCommand();
  TEST_ASSERT_EQUAL_INT(1, WiFi.disconnects);
  request({{"deletelog", "neither"}}); handleCommand();
  TEST_ASSERT_EQUAL_INT(400, server.responses.front().code);
  request({{"bogus", ""}}); handleCommand();
  TEST_ASSERT_EQUAL_STRING("Unrecognized request", server.responses.back().body.c_str());
}

static void postPasswords(const std::string& json){ request({{"plain", b64(json)}}); handlePasswords(); }

static void test_passwords(void){
  postPasswords("{\"newadmin\":\"secret\",\"newuser\":\"viewer\",\"localAccess\":\"true\"}");   // no admin yet
  TEST_ASSERT_EQUAL_STRING("Passwords reset.", server.responses.back().body.c_str());
  TEST_ASSERT_NOT_NULL(adminH1); TEST_ASSERT_NOT_NULL(userH1);
  TEST_ASSERT_TRUE(localAccess);                                       // quoted "true" (jBool)
  TEST_ASSERT_EQUAL_INT(1, hostSavedPwds);
  TEST_ASSERT_EQUAL_STRING(calcH1("admin", deviceName, "secret").c_str(), bin2hex(adminH1, 16).c_str());
  postPasswords("{\"newadmin\":\"x\"}");                               // B10: current password required
  TEST_ASSERT_EQUAL_INT(400, server.responses.back().code);
  postPasswords("{\"oldadmin\":\"wrong\",\"newadmin\":\"x\"}");
  TEST_ASSERT_EQUAL_STRING("Current admin password invalid.", server.responses.back().body.c_str());
  postPasswords("{\"oldadmin\":\"secret\",\"newadmin\":\"\"}");       // correct: clears the passwords
  TEST_ASSERT_EQUAL_STRING("Passwords reset.", server.responses.back().body.c_str());
  TEST_ASSERT_NULL(adminH1); TEST_ASSERT_NULL(userH1);
  request({{"plain", b64("{not json")}}); handlePasswords();
  TEST_ASSERT_EQUAL_INT(400, server.responses.back().code);
}

static void test_feed_list(void){
  request({}); handleGetFeedList();
  JsonDocument d = lastJson();
  TEST_ASSERT_EQUAL_STRING("IVVolts", d[0]["id"].as<const char*>());   // VT: one voltage feed
  TEST_ASSERT_EQUAL_STRING("IPMain", d[1]["id"].as<const char*>());    // CT: power and energy feeds
  TEST_ASSERT_EQUAL_STRING("IEMain", d[2]["id"].as<const char*>());
  bool total = false; for(JsonObject o : d.as<JsonArray>()) total |= String(o["id"].as<const char*>()) == "OPTotal";
  TEST_ASSERT_TRUE(total);
}

static void test_graphs(void){
  SD.reset(); SD.mkdir("graphs");
  // Graph+ saves graphs without "start" (listed by getallplus); the original Graph page with it (getall).
  request({{"data", "{\"name\":\"Daily\",\"units\":\"Watts\"}"}}); handleGraphCreate();
  JsonDocument d = lastJson();
  TEST_ASSERT_TRUE(d["success"].as<bool>());
  std::string id = String(d["message"].as<const char*>()).substring(15).str();      // "graph saved id:XXXX"
  request({{"data", "{\"name\":\"Classic\",\"start\":\"d\"}"}}); handleGraphCreate();
  request({}); handleGraphGetallplus();
  JsonDocument plus = lastJson();
  TEST_ASSERT_EQUAL_INT(1, plus.size());
  TEST_ASSERT_EQUAL_STRING("Daily", plus[0]["name"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING((id + ".txt").c_str(), plus[0]["id"].as<const char*>());
  request({}); handleGraphGetall();
  JsonDocument all = lastJson();
  TEST_ASSERT_EQUAL_INT(1, all.size());
  TEST_ASSERT_EQUAL_STRING("Classic", all[0]["name"].as<const char*>());
  request({{"data", "{not json"}}); handleGraphCreate();
  TEST_ASSERT_EQUAL_INT(400, server.responses.back().code);            // B11
  request({{"data", "{\"units\":\"Watts\"}"}}); handleGraphCreate();
  TEST_ASSERT_EQUAL_INT(400, server.responses.back().code);            // B11: no name
  request({{"id", id + ".txt"}}); handleGraphDelete();             // the UI sends back the listed id
  request({}); handleGraphGetallplus();
  TEST_ASSERT_EQUAL_STRING("[]", server.responses.back().body.c_str());
}

// B53: bad files in /graphs (empty, a few bytes, not JSON) must not crash either list.
static void test_graph_lists_survive_bad_files(void){
  SD.reset(); SD.mkdir("graphs");
  SD.create("/graphs/empty.txt");
  auto d = SD.create("/graphs/short.txt"); d->writeAt(0, (const uint8_t*)"{}", 2); d->size = 2;
  auto e = SD.create("/graphs/junk.txt"); e->writeAt(0, (const uint8_t*)"not json at all, \"start\"", 24); e->size = 24;
  request({}); handleGraphGetall();
  TEST_ASSERT_EQUAL_INT(200, server.responses.back().code);
  request({}); handleGraphGetallplus();
  TEST_ASSERT_EQUAL_INT(200, server.responses.back().code);
}

static void test_directory_listing(void){
  SD.reset(); SD.mkdir("/iotawatt");
  SD.create("/index.htm"); SD.create("/iotawatt/iotamsgs.txt");
  request({{"dir", "/"}}); printDirectory();
  JsonDocument d = lastJson();
  TEST_ASSERT_EQUAL_STRING("esp_spiffs", d[0]["name"].as<const char*>());
  bool dir = false, file = false;
  for(JsonObject o : d.as<JsonArray>()){
    dir |= String(o["name"].as<const char*>()) == "iotawatt" && String(o["type"].as<const char*>()) == "dir";
    file |= String(o["name"].as<const char*>()) == "index.htm" && String(o["type"].as<const char*>()) == "file";
  }
  TEST_ASSERT_TRUE(dir); TEST_ASSERT_TRUE(file);
  request({{"dir", "/nope"}}); printDirectory();
  TEST_ASSERT_EQUAL_INT(500, server.responses.back().code);
}

// B27 at the web layer: a headerless JSON /query past the time budget aborts the transfer.
static void test_query_streaming_and_timeout_abort(void){
  request({{"select", "[time.utc.unix,Main.watts]"}, {"begin", std::to_string(T0)}, {"end", std::to_string(T0 + 7200)},
           {"group", "1h"}, {"format", "json"}});
  handleQuery();
  JsonDocument d; TEST_ASSERT_FALSE_MESSAGE(deserializeJson(d, server.content), server.content.c_str());
  TEST_ASSERT_EQUAL_INT(2, d.size());
  TEST_ASSERT_FALSE(server.client().stopped);
  // Past the budget: each streamed chunk takes 30 s, so the 60 s budget runs out mid-transfer.
  request({{"select", "[time.utc.unix,Main.watts]"}, {"begin", std::to_string(T0)}, {"end", std::to_string(T0 + 3 * 3600)},
           {"group", "5s"}, {"format", "json"}, {"limit", "none"}});
  server.millisPerChunk = 30000;
  handleQuery();
  TEST_ASSERT_TRUE(server.client().stopped);                            // transfer aborted: no final chunk
  JsonDocument partial; TEST_ASSERT_TRUE((bool)deserializeJson(partial, server.content));   // incomplete, not a valid array
  bool logged = false; for(auto& l : hostLog) logged |= l.find("time limit reached") != std::string::npos;
  TEST_ASSERT_TRUE(logged);
}

// ---- InfluxDB v1 reconfiguration (here, not in test_uploaders: that suite replaces operator
// new/delete to count heap use, which hides allocation mismatches from AddressSanitizer) ----
// B63: a new configuration with a retention policy freed the old one with delete instead of
// delete[] (AddressSanitizer: alloc-dealloc-mismatch).
static void test_influxdb_v1_reconfig_retention(void){
  influxDB_uploader* u = new influxDB_uploader;
  for(int rev = 1; rev <= 2; rev++){
    std::string cfg = "{\"type\":\"influxdb\",\"revision\":" + std::to_string(rev) + ",\"url\":\"http://influx.lan:8086\","
      "\"database\":\"iotawatt\",\"retp\":\"autogen\",\"postInterval\":10,"
      "\"outputs\":[{\"name\":\"Main\",\"units\":\"Watts\",\"script\":\"@1\"}]}";
    TEST_ASSERT_TRUE(u->config(cfg.c_str()));
  }
  delete u;
  for(serviceBlock* b : hostServices) delete b;                     // the stand-in scheduler's blocks
  hostServices.clear();
}

// The data plane (user login): readings and status only. Directory listings and diagnostics need admin
// (Docs/api.rst); the endpoints Home Assistant and other integrations read stay user-level.
static void test_user_login_gets_data_only(void){
  hostAdminResult = false;
  auto get = [](const char* uri, std::initializer_list<std::pair<const char*, std::string>> a) -> int {
    request(a); server._uri = uri; server._method = HTTP_GET; webGuard = WebGuard(); handleRequest();
    return server.responses.empty() ? 0 : server.responses.back().code;
  };
  TEST_ASSERT_EQUAL_INT(401, get("/list", {{"dir", "/iotawatt"}}));
  TEST_ASSERT_EQUAL_INT(401, get("/DSTtest", {{"begin", "0"}, {"end", "0"}}));
  TEST_ASSERT_EQUAL_INT(401, get("/config.txt", {}));
  TEST_ASSERT_EQUAL_INT(200, get("/status", {{"wifi", "yes"}}));
  TEST_ASSERT_EQUAL_INT(200, get("/status", {{"inputs", "yes"}, {"outputs", "yes"}}));
  TEST_ASSERT_EQUAL_INT(200, get("/feed/list.json", {}));
  TEST_ASSERT_NOT_EQUAL(401, get("/query", {{"show", "series"}}));
  hostAdminResult = true;
}

// ---- overload guard (webGuard.cpp) ----
static void test_guard_cost(void){
  TEST_ASSERT_EQUAL_INT(webHeavy, webRequestCost("/status"));
  TEST_ASSERT_EQUAL_INT(webHeavy, webRequestCost("/query"));
  TEST_ASSERT_EQUAL_INT(webHeavy, webRequestCost("/feed/data.json"));
  TEST_ASSERT_EQUAL_INT(webHeavy, webRequestCost("/graph/getallplus"));
  TEST_ASSERT_EQUAL_INT(webLight, webRequestCost("/"));
  TEST_ASSERT_EQUAL_INT(webLight, webRequestCost("/index.htm"));
  TEST_ASSERT_EQUAL_INT(webLight, webRequestCost("/statusx"));
  TEST_ASSERT_EQUAL_INT(webLight, webRequestCost("/nullreq"));
  TEST_ASSERT_EQUAL_INT(webLight, webRequestCost(nullptr));
  TEST_ASSERT_EQUAL_INT(webLight, webRequestCost("/status", true));    // the menu bar's /status?device=yes
  TEST_ASSERT_EQUAL_INT(webHeavy, webRequestCost("/query", true));
}

// A burst of WEB_GUARD_BURST heavy requests passes, the next is too fast; tokens come back one per
// WEB_GUARD_REFILL_MS, never more than the burst, also across the millis() wrap. Light requests
// never take a token.
static void test_guard_bucket(void){
  for(uint32_t start : {1000u, 0xFFFFF000u}){
    WebGuard g; uint32_t t = start;
    for(int i = 0; i < WEB_GUARD_BURST; i++) TEST_ASSERT_EQUAL_INT(webGuardOK, webGuardCheck(g, webHeavy, 40000, t));
    TEST_ASSERT_EQUAL_INT(webGuardTooFast, webGuardCheck(g, webHeavy, 40000, t));
    TEST_ASSERT_EQUAL_INT(webGuardOK, webGuardCheck(g, webLight, 40000, t));
    t += WEB_GUARD_REFILL_MS - 1;
    TEST_ASSERT_EQUAL_INT(webGuardTooFast, webGuardCheck(g, webHeavy, 40000, t));
    t += 1;
    TEST_ASSERT_EQUAL_INT(webGuardOK, webGuardCheck(g, webHeavy, 40000, t));
    TEST_ASSERT_EQUAL_INT(webGuardTooFast, webGuardCheck(g, webHeavy, 40000, t));
    t += 3600000;                                                      // an hour idle: full burst, no more
    for(int i = 0; i < WEB_GUARD_BURST; i++) TEST_ASSERT_EQUAL_INT(webGuardOK, webGuardCheck(g, webHeavy, 40000, t));
    TEST_ASSERT_EQUAL_INT(webGuardTooFast, webGuardCheck(g, webHeavy, 40000, t));
    for(int i = 0; i < 50; i++){                                      // steady 1 per refill period: all pass
      t += WEB_GUARD_REFILL_MS;
      TEST_ASSERT_EQUAL_INT(webGuardOK, webGuardCheck(g, webHeavy, 40000, t));
    }
  }
}

// Each caller has its own bucket: one polling too fast doesn't take the others' requests. A fifth
// caller reuses the least recently used bucket (fresh, full).
static void test_guard_per_client(void){
  WebGuard g; uint32_t t = 1000;
  const uint32_t ha = 0x0A01A8C0, ui = 0x0B01A8C0, script = 0x0C01A8C0;
  for(int i = 0; i < 50; i++) webGuardCheck(g, webHeavy, 40000, t, script);       // flat out
  TEST_ASSERT_EQUAL_INT(webGuardTooFast, webGuardCheck(g, webHeavy, 40000, t, script));
  for(int i = 0; i < WEB_GUARD_BURST; i++){
    TEST_ASSERT_EQUAL_INT(webGuardOK, webGuardCheck(g, webHeavy, 40000, t, ha));
    TEST_ASSERT_EQUAL_INT(webGuardOK, webGuardCheck(g, webHeavy, 40000, t, ui));
  }
  TEST_ASSERT_EQUAL_INT(webGuardTooFast, webGuardCheck(g, webHeavy, 40000, t, ha));
  t += 10;
  webGuardCheck(g, webHeavy, 40000, t, 4);                                         // 4th caller
  t += 10;
  webGuardCheck(g, webHeavy, 40000, t, 5);                                         // 5th: reuses script's (oldest)
  t += 10;
  TEST_ASSERT_EQUAL_INT(webGuardOK, webGuardCheck(g, webHeavy, 40000, t, script)); // back fresh, evicting ha
  TEST_ASSERT_EQUAL_INT(webGuardOK, webGuardCheck(g, webHeavy, 40000, t, ha));     // ha too (fresh again)
}

static void test_guard_low_heap(void){
  WebGuard g;
  TEST_ASSERT_EQUAL_INT(webGuardLowHeap, webGuardCheck(g, webLight, WEB_GUARD_MIN_HEAP - 1, 0));
  TEST_ASSERT_EQUAL_INT(webGuardOK, webGuardCheck(g, webLight, WEB_GUARD_MIN_HEAP, 0));
  TEST_ASSERT_EQUAL_INT(webGuardLowHeap, webGuardCheck(g, webHeavy, WEB_GUARD_MIN_HEAP_HEAVY - 1, 0));
  TEST_ASSERT_EQUAL_INT(webGuardOK, webGuardCheck(g, webHeavy, WEB_GUARD_MIN_HEAP_HEAVY, 0));
  TEST_ASSERT_EQUAL_UINT32(5, webGuardRetryAfter(webGuardLowHeap));
  TEST_ASSERT_EQUAL_UINT32(1, webGuardRetryAfter(webGuardTooFast));
}

// Through the dispatcher: a caller polling /status too fast gets 503 + Retry-After, the web UI is
// still served, and low memory refuses everything; refusals are logged at most once a minute.
static void test_guard_in_handleRequest(void){
  SD.reset(); hostLog.clear(); webGuard = WebGuard(); ESP.freeHeap = 40000; hostMillis = 5000;
  auto get = [](const char* uri) -> const HostResponse& {
    request({}); server._uri = uri; server._method = HTTP_GET; handleRequest();
    TEST_ASSERT_FALSE(server.responses.empty()); return server.responses.back();
  };
  for(int i = 0; i < WEB_GUARD_BURST; i++) TEST_ASSERT_EQUAL_INT(200, get("/status").code);
  const HostResponse& r = get("/status");
  TEST_ASSERT_EQUAL_INT(503, r.code);
  TEST_ASSERT_EQUAL_STRING("1", server.sentHeaders["Retry-After"].c_str());
  TEST_ASSERT_EQUAL_INT(503, get("/query").code);
  TEST_ASSERT_EQUAL_INT(200, get("/").code);                          // embedded web UI: light
  hostMillis += WEB_GUARD_REFILL_MS;
  TEST_ASSERT_EQUAL_INT(200, get("/status").code);
  size_t logged = 0; for(auto& l : hostLog) logged += l.find("webServer: busy") != std::string::npos;
  TEST_ASSERT_EQUAL_INT(1, logged);                                    // once, not per refusal

  ESP.freeHeap = WEB_GUARD_MIN_HEAP - 1; hostMillis += 60000;
  TEST_ASSERT_EQUAL_INT(503, get("/").code);
  TEST_ASSERT_EQUAL_STRING("5", server.sentHeaders["Retry-After"].c_str());
  logged = 0; for(auto& l : hostLog) logged += l.find("low memory") != std::string::npos;
  TEST_ASSERT_EQUAL_INT(2, logged);
  ESP.freeHeap = 40000; webGuard = WebGuard();

        // Other callers aren't refused because one polls too fast (B78); the menu bar's device status
        // is light (B79); refusals after the last log line (the first was logged at once, 3 more
        // since) are logged by the next request a minute on (B81).
  hostLog.clear();
  for(int i = 0; i < WEB_GUARD_BURST + 3; i++) get("/status");                    // 192.168.1.10, flat out
  TEST_ASSERT_EQUAL_INT(503, get("/status").code);
  request({}); server._uri = "/status"; server._method = HTTP_GET; server._client.ip = IPAddress(192, 168, 1, 20);
  handleRequest();
  TEST_ASSERT_EQUAL_INT(200, server.responses.back().code);
  request({{"device", "yes"}}); server._uri = "/status"; server._method = HTTP_GET; handleRequest();
  TEST_ASSERT_EQUAL_INT(200, server.responses.back().code);                        // .10 again, but light
  size_t before = hostLog.size();
  hostMillis += 60000;
  TEST_ASSERT_EQUAL_INT(200, get("/").code);
  TEST_ASSERT_EQUAL_INT((int)before + 1, (int)hostLog.size());
  TEST_ASSERT_TRUE(hostLog.back().find("and 3 for coming too fast") != std::string::npos);
  ESP.freeHeap = 40000; webGuard = WebGuard();
}

// ---- embedded web UI (webui.cpp) ----
static std::string gunzip(const uint8_t* d, size_t n){
  z_stream z{}; inflateInit2(&z, 16 + MAX_WBITS);
  z.next_in = (Bytef*)d; z.avail_in = n; std::string out; char buf[16384]; int rc;
  do { z.next_out = (Bytef*)buf; z.avail_out = sizeof buf; rc = inflate(&z, Z_NO_FLUSH); out.append(buf, sizeof buf - z.avail_out); }
  while(rc == Z_OK);
  inflateEnd(&z); TEST_ASSERT_EQUAL_INT(Z_STREAM_END, rc); return out;
}
static std::string readRepoFile(const char* name){
  std::ifstream f(std::string("../SD/") + name, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str();
}

// Every embedded file decompresses to exactly the SD/ source it was built from.
static void test_webui_embedded_matches_sources(void){
  TEST_ASSERT_EQUAL_INT(8, WEBUI_COUNT);
  for(int i = 0; i < WEBUI_COUNT; i++){
    const webuiFile& f = webuiFiles[i];
    std::string src = readRepoFile(f.name);
    TEST_ASSERT_TRUE_MESSAGE(src.size() > 0, f.name);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(src.size(), f.len, f.name);
    TEST_ASSERT_TRUE_MESSAGE(gunzip(f.gz, f.gzLen) == src, f.name);
  }
}

static void test_webui_find(void){
  TEST_ASSERT_NOT_NULL(webuiFind("/index.htm"));
  TEST_ASSERT_NOT_NULL(webuiFind("/INDEX.HTM"));                       // FAT names are case-insensitive
  TEST_ASSERT_NOT_NULL(webuiFind("graph2.js"));
  TEST_ASSERT_EQUAL_STRING("text/css", webuiFind("/CnfStyle.css")->type);
  TEST_ASSERT_EQUAL_STRING("text/css", webuiFind("/bootstrap.min.css")->type);
  TEST_ASSERT_EQUAL_STRING("application/javascript", webuiFind("/bootstrap.min.js")->type);
  TEST_ASSERT_EQUAL_STRING("application/javascript", webuiFind("/iotanav.js")->type);
  const char* not_embedded[] = {"/config.txt", "/tables.txt", "/favicon.ico", "/", "", "/index.htm/", "/index.html",
                                "/iotawatt/index.htm", "/index.htm.src", "/index", "/graph.htm", "/graph.js", nullptr};
  for(int i = 0; not_embedded[i]; i++) TEST_ASSERT_NULL_MESSAGE(webuiFind(not_embedded[i]), not_embedded[i]);
  TEST_ASSERT_NULL(webuiFind(nullptr));
}

static const HostResponse& getPath(const char* uri, std::initializer_list<std::pair<const char*, std::string>> a = {}){
  request(a); server._uri = uri; server.reqHeaders["Accept-Encoding"] = "gzip, deflate";   // as browsers send
  TEST_ASSERT_TRUE(loadFromSdCard(String(uri)));
  TEST_ASSERT_FALSE(server.responses.empty()); return server.responses.back();
}

// Default: the embedded copy is served (gzip), whether or not the SD card has one.
static void test_webui_served_embedded(void){
  SD.reset(); webuiFromSD = false;
  const HostResponse& r = getPath("/");
  TEST_ASSERT_EQUAL_INT(200, r.code); TEST_ASSERT_EQUAL_STRING("text/html", r.type.c_str());
  TEST_ASSERT_EQUAL_STRING("gzip", server.sentHeaders["Content-Encoding"].c_str());
  TEST_ASSERT_TRUE(gunzip((const uint8_t*)r.body.data(), r.body.size()) == readRepoFile("index.htm"));
  auto f = SD.create("/graph2.js"); f->writeAt(0, (const uint8_t*)"stale", 5); f->size = 5;
  const HostResponse& g = getPath("/graph2.js");
  TEST_ASSERT_EQUAL_STRING("application/javascript", g.type.c_str());
  TEST_ASSERT_TRUE(gunzip((const uint8_t*)g.body.data(), g.body.size()) == readRepoFile("graph2.js"));
  TEST_ASSERT_TRUE(getPath("/edit").body.size() > 0);                  // /edit -> edit.htm, embedded
  TEST_ASSERT_EQUAL_STRING("gzip", server.sentHeaders["Content-Encoding"].c_str());
}

// B75: a client that doesn't accept gzip gets the SD copy when there is one (else still the gzip);
// ?download=true downloads the embedded copy.
static void test_webui_without_gzip(void){
  SD.reset(); webuiFromSD = false;
  auto f = SD.create("/graph2.js"); f->writeAt(0, (const uint8_t*)"plain js", 8); f->size = 8;
  request({}); server._uri = "/graph2.js"; TEST_ASSERT_TRUE(loadFromSdCard(String("/graph2.js")));
  TEST_ASSERT_EQUAL_STRING("plain js", server._client.written.c_str());
  TEST_ASSERT_EQUAL_INT(0, server.sentHeaders.count("Content-Encoding"));
  request({}); server._uri = "/index.htm"; TEST_ASSERT_TRUE(loadFromSdCard(String("/index.htm")));   // no SD copy
  TEST_ASSERT_EQUAL_STRING("gzip", server.sentHeaders["Content-Encoding"].c_str());
  const HostResponse& d = getPath("/index.htm", {{"download", "true"}});
  TEST_ASSERT_EQUAL_STRING("application/octet-stream", d.type.c_str());
}

// Override on: the SD copy wins when present; embedded is the fallback and ?ui=embedded the rescue.
static void test_webui_sd_override(void){
  SD.reset(); webuiFromSD = true;
  auto f = SD.create("/index.htm"); f->writeAt(0, (const uint8_t*)"<html>custom</html>", 19); f->size = 19;
  getPath("/");
  TEST_ASSERT_EQUAL_STRING("<html>custom</html>", server._client.written.c_str());   // streamed from SD
  TEST_ASSERT_EQUAL_INT(0, server.sentHeaders.count("Content-Encoding"));
  const HostResponse& r = getPath("/", {{"ui", "embedded"}});
  TEST_ASSERT_EQUAL_STRING("gzip", server.sentHeaders["Content-Encoding"].c_str());
  TEST_ASSERT_TRUE(gunzip((const uint8_t*)r.body.data(), r.body.size()) == readRepoFile("index.htm"));
  const HostResponse& c = getPath("/cnfstyle.css");                    // not on SD: embedded fallback
  TEST_ASSERT_TRUE(gunzip((const uint8_t*)c.body.data(), c.body.size()) == readRepoFile("cnfstyle.css"));
  webuiFromSD = false;
}

// Non-UI files are never answered from the firmware.
static void test_webui_leaves_other_files_alone(void){
  SD.reset(); webuiFromSD = false;
  auto f = SD.create("/tables.txt"); f->writeAt(0, (const uint8_t*)"{}", 2); f->size = 2;
  getPath("/tables.txt");
  TEST_ASSERT_EQUAL_STRING("{}", server._client.written.c_str());
  request({}); server._uri = "/favicon.ico";
  TEST_ASSERT_FALSE(loadFromSdCard(String("/favicon.ico")));          // not on SD, not embedded: 404 path
}

// The original Graph page is retired: links to it go to Graph+, unless the SD override is on and
// the card still has a copy (kept on purpose). graph.js, used only by that page, is just gone.
static void test_webui_retired_graph_redirects(void){
  SD.reset(); webuiFromSD = false;
  const HostResponse& r = getPath("/graph.htm");
  TEST_ASSERT_EQUAL_INT(302, r.code);
  TEST_ASSERT_EQUAL_STRING("/graph2.htm", server.sentHeaders["Location"].c_str());
  getPath("/GRAPH.HTM");
  TEST_ASSERT_EQUAL_INT(302, server.responses.back().code);
  auto f = SD.create("/graph.htm"); f->writeAt(0, (const uint8_t*)"<html>old</html>", 16); f->size = 16;
  TEST_ASSERT_EQUAL_INT(302, getPath("/graph.htm").code);             // default: even with a stale SD copy
  webuiFromSD = true;
  server.sentHeaders.clear();
  getPath("/graph.htm");
  TEST_ASSERT_EQUAL_STRING("<html>old</html>", server._client.written.c_str());   // override: SD copy served
  TEST_ASSERT_EQUAL_INT(0, server.sentHeaders.count("Location"));
  webuiFromSD = false;
  request({}); server._uri = "/graph.js";
  TEST_ASSERT_FALSE(loadFromSdCard(String("/graph.js")));              // not on SD, not embedded: 404 path
  TEST_ASSERT_NULL(webuiRedirect(nullptr, false));
  TEST_ASSERT_NULL(webuiRedirect("/graph2.htm", false));
}

int main(int, char**){
  setenv("TZ", "UTC", 1); tzset();
  struct tm t{}; t.tm_year = 2026 - 1900; t.tm_mon = 10; t.tm_mday = 23; T0 = (uint32_t)timegm(&t);
  hostUTC = T0 + 3 * 3600;
  for(int i = 0; i < 3; i++){ chans[i] = new IotaInputChannel(i); chans[i]->_vmult = 1; chans[i]->active(true); }
  chans[0]->_type = channelTypeVoltage; chans[0]->_name = charstar("Volts");
  chans[1]->_type = channelTypePower;   chans[1]->_name = charstar("Main");
  chans[2]->_type = channelTypePower;   chans[2]->_name = charstar("Spare");
  inputChannel = chans; maxInputs = 3;
  JsonDocument od; deserializeJson(od, "[{\"name\":\"Total\",\"units\":\"Watts\",\"script\":\"@1+@2\"}]");
  delete outputs; outputs = new ScriptSet(od.as<JsonArray>());
  integrations = new ScriptSet();
  SD.reset(); Current_log.begin("/iotawatt/iotalog.log");
  IotaLogRecord r; double wh = 0, h = 0;
  for(uint32_t ts = T0; ts <= T0 + 3 * 3600; ts += 5){ if(ts > T0){ h += 5.0 / 3600; wh += 500 * 5.0 / 3600; }
    r.UNIXtime = ts; r.logHours = h; r.accum1[0] = 120 * h; r.accum1[1] = wh; r.accum2[1] = wh; Current_log.write(&r); }
  UNITY_BEGIN();
  RUN_TEST(test_status_stats_wifi_passwords_device);
  RUN_TEST(test_status_inputs_and_outputs);
  RUN_TEST(test_status_uploaders_and_datalogs);
  RUN_TEST(test_vcal);
  RUN_TEST(test_command);
  RUN_TEST(test_passwords);
  RUN_TEST(test_feed_list);
  RUN_TEST(test_graphs);
  RUN_TEST(test_graph_lists_survive_bad_files);
  RUN_TEST(test_directory_listing);
  RUN_TEST(test_query_streaming_and_timeout_abort);
  RUN_TEST(test_influxdb_v1_reconfig_retention);
  RUN_TEST(test_user_login_gets_data_only);
  RUN_TEST(test_guard_cost);
  RUN_TEST(test_guard_bucket);
  RUN_TEST(test_guard_per_client);
  RUN_TEST(test_guard_low_heap);
  RUN_TEST(test_guard_in_handleRequest);
  RUN_TEST(test_webui_embedded_matches_sources);
  RUN_TEST(test_webui_find);
  RUN_TEST(test_webui_served_embedded);
  RUN_TEST(test_webui_without_gzip);
  RUN_TEST(test_webui_sd_override);
  RUN_TEST(test_webui_leaves_other_files_alone);
  RUN_TEST(test_webui_retired_graph_redirects);
  return UNITY_END();
}
