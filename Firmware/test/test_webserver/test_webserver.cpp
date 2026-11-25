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
#include "uploaders/influxDB_uploader.cpp"
#include "CSVquery.cpp"
#include "webServer.cpp"
#include "graphSave.cpp"
#include "webui.cpp"
#include <zlib.h>
#include <fstream>
#include <sstream>
bool auth(authLevel) { return hostAuthResult; }          // auth.cpp isn't under test here

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
  TEST_ASSERT_EQUAL_INT(5, WEBUI_COUNT);
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
  RUN_TEST(test_webui_embedded_matches_sources);
  RUN_TEST(test_webui_find);
  RUN_TEST(test_webui_served_embedded);
  RUN_TEST(test_webui_without_gzip);
  RUN_TEST(test_webui_sd_override);
  RUN_TEST(test_webui_leaves_other_files_alone);
  RUN_TEST(test_webui_retired_graph_redirects);
  return UNITY_END();
}
