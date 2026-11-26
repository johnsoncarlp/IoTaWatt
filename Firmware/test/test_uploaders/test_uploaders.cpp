// Uploaders (influxDB v1/v2, Emoncms) driven through their real state machines: every HTTP request they
// make is recorded (method, URL, headers, body) and answered with a scripted reply.
#include <unity.h>
#include <atomic>
#include <new>
#include "host_uploader_deps.h"
// Fake scheduler blocks and recorded requests are the harness's; firmware leaks are measured by
// test_restart_does_not_leak with an allocation count instead.
extern "C" const char* __asan_default_options() { return "detect_leaks=0"; }
static std::atomic<long> liveAllocs{0};
void* operator new(size_t n){ liveAllocs++; void* p = malloc(n ? n : 1); if(!p) throw std::bad_alloc(); return p; }
void* operator new[](size_t n){ liveAllocs++; void* p = malloc(n ? n : 1); if(!p) throw std::bad_alloc(); return p; }
void operator delete(void* p) noexcept { if(p){ liveAllocs--; free(p); } }
void operator delete[](void* p) noexcept { if(p){ liveAllocs--; free(p); } }
void operator delete(void* p, size_t) noexcept { if(p){ liveAllocs--; free(p); } }
void operator delete[](void* p, size_t) noexcept { if(p){ liveAllocs--; free(p); } }
#include "IotaLog.cpp"
#include "IotaScript.cpp"
#include "timeRules.cpp"
#include "utilities.cpp"
#include "xurl.cpp"
#include "splitstr.cpp"
#include "uploaders/Uploader.cpp"
#include "uploaders/influxDB_uploader.cpp"
#include "uploaders/influxDB2_uploader.cpp"
#include "uploaders/Emoncms_uploader.cpp"

static uint32_t T0;                                          // 2026-11-23 00:00 UTC
static IotaInputChannel* chans[2];
static double wattsAt(uint32_t t){ return 100.0 * (1 + ((t - T0) / 600) % 6); }   // changes every 10 minutes

static void buildLog(uint32_t hours){
  SD.reset();
  Current_log.~IotaLog(); new (&Current_log) IotaLog(256, 5, 365, 24);
  Current_log.begin("/iotawatt/iotalog.log");
  IotaLogRecord r; double wh = 0, vh = 0, hzh = 0, h = 0;
  for(uint32_t t = T0; t <= T0 + hours * 3600; t += 5){
    if(t > T0){ double dh = 5.0 / 3600; h += dh; wh += wattsAt(t - 5) * dh; vh += 120.0 * dh; hzh += 60.0 * dh; }
    r.UNIXtime = t; r.logHours = h; r.accum1[0] = vh; r.accum2[0] = hzh; r.accum1[1] = wh; r.accum2[1] = wh / 0.8;
    Current_log.write(&r);
  }
}

void setUp(void){
  hostHttpLog.clear(); hostHttpReplies.clear(); hostServices.clear(); hostLog.clear();
  HTTPSproxy = nullptr; hostUTC = T0 + 2 * 3600; WiFi.connected = true; hostRandomSeed = 1;
}
void tearDown(void){}

// Run the uploader's service until n requests have been made (or a step limit).
static void runUntil(serviceBlock* sb, size_t requests, int maxSteps = 5000){
  for(int i = 0; i < maxSteps && hostHttpLog.size() < requests; i++) sb->service(sb);
}
static std::vector<std::string> lines(const std::string& s){
  std::vector<std::string> v; size_t a = 0, b;
  while((b = s.find('\n', a)) != std::string::npos){ v.push_back(s.substr(a, b - a)); a = b + 1; }
  if(a < s.size()) v.push_back(s.substr(a));
  return v;
}
#include "emoncms_golden.inc"
static const char* OUTPUTS = "[{\"name\":\"Main\",\"units\":\"Watts\",\"script\":\"@1\"},{\"name\":\"Volts\",\"units\":\"Volts\",\"script\":\"@0\"}]";

static void test_influxdb_v1_query_and_write(void){
  std::string cfg = std::string("{\"type\":\"influxdb\",\"revision\":1,\"url\":\"http://influx.lan:8086\",\"database\":\"iotawatt\","
    "\"postInterval\":10,\"bulksend\":2,\"user\":\"u\",\"pwd\":\"p\",\"measurement\":\"$name\",\"fieldkey\":\"value\","
    "\"tagset\":[{\"key\":\"device\",\"value\":\"iotawatt\"}],\"outputs\":") + OUTPUTS + "}";
  influxDB_uploader* u = new influxDB_uploader;
  TEST_ASSERT_TRUE(u->config(cfg.c_str()));
  TEST_ASSERT_EQUAL_INT(1, hostServices.size());
  uint32_t resume = T0 + 3600;                               // last point already in Influx
  for(const char* m : {"Main", "Volts"}){
    char r[256]; snprintf(r, sizeof r, "{\"results\":[{\"statement_id\":0,\"series\":[{\"name\":\"%s\",\"columns\":[\"time\",\"last\"],\"values\":[[%u,1]]}]}]}", m, resume);
    hostHttpReplies.push_back({200, r});
  }
  hostHttpReplies.push_back({204, ""});
  runUntil(hostServices.back(), 3);
  TEST_ASSERT_EQUAL_INT(3, hostHttpLog.size());
  const HostHttpRequest& q = hostHttpLog[0];
  TEST_ASSERT_EQUAL_STRING("POST", q.method.c_str());
  TEST_ASSERT_EQUAL_STRING("http://influx.lan:8086/query", q.url.c_str());
  TEST_ASSERT_EQUAL_STRING("db=iotawatt&epoch=s&q=SELECT+LAST%28%22value%22%29+FROM+%22Main%22+WHERE+%22device%22%3D%27iotawatt%27", q.body.c_str());
  TEST_ASSERT_EQUAL_STRING("Basic dTpw", q.headers.at("Authorization").c_str());          // base64("u:p")
  const HostHttpRequest& w = hostHttpLog[2];
  TEST_ASSERT_EQUAL_STRING("http://influx.lan:8086/write?precision=s&db=iotawatt", w.url.c_str());
  auto L = lines(w.body);
  TEST_ASSERT_TRUE(L.size() >= 4);
  for(size_t i = 0; i + 1 < L.size(); i += 2){
    uint32_t t = resume + 10 * (uint32_t)(i / 2 + 1);       // resume is already in Influx: start one interval later
    char want[160];
    snprintf(want, sizeof want, "Main,device=iotawatt value=%.2f %u", wattsAt(t), t);
    TEST_ASSERT_EQUAL_STRING(want, L[i].c_str());
    snprintf(want, sizeof want, "Volts,device=iotawatt value=120.00 %u", t);
    TEST_ASSERT_EQUAL_STRING(want, L[i + 1].c_str());
  }
  delete u;
}

// B50: names with line-protocol and query syntax in them are escaped/quoted. Expected strings computed
// independently in Python (urllib.parse.quote_plus; InfluxQL/Flux/line-protocol escaping rules).
static const char* HOSTILE_OUT = "[{\"name\":\"Main\",\"units\":\"Watts\",\"script\":\"@1\"}]";
static const char* HOSTILE_KEYS = "\"measurement\":\"Main Panel\",\"fieldkey\":\"kW,net\","
  "\"tagset\":[{\"key\":\"loc=x\",\"value\":\"O'Brien {a} \\\"q\\\"\"}]";

static void test_influx_escaping_v1(void){
  std::string cfg = std::string("{\"type\":\"influxdb\",\"revision\":1,\"url\":\"http://influx.lan:8086\",\"database\":\"iotawatt\","
    "\"postInterval\":10,\"bulksend\":1,") + HOSTILE_KEYS + ",\"outputs\":" + HOSTILE_OUT + "}";
  influxDB_uploader* u = new influxDB_uploader;
  TEST_ASSERT_TRUE(u->config(cfg.c_str()));
  char r[256]; snprintf(r, sizeof r, "{\"results\":[{\"statement_id\":0,\"series\":[{\"name\":\"Main Panel\",\"columns\":[\"time\",\"last\"],\"values\":[[%u,1]]}]}]}", T0 + 3600);
  hostHttpReplies.push_back({200, r});
  hostHttpReplies.push_back({204, ""});
  runUntil(hostServices.back(), 2);
  TEST_ASSERT_EQUAL_INT(2, hostHttpLog.size());
  TEST_ASSERT_EQUAL_STRING("db=iotawatt&epoch=s&q=SELECT+LAST%28%22kW%2Cnet%22%29+FROM+%22Main+Panel%22+WHERE+%22loc%3Dx%22%3D%27O%5C%27Brien+%7Ba%7D+%22q%22%27",
                           hostHttpLog[0].body.c_str());
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, hostHttpLog[1].body.rfind("Main\\ Panel,loc\\=x=O'Brien\\ {a}\\ \"q\" kW\\,net=", 0), hostHttpLog[1].body.c_str());
  delete u;
}

static void test_influx_escaping_v2(void){
  std::string cfg = std::string("{\"type\":\"influxdb2\",\"revision\":1,\"url\":\"http://influx2.lan:8086\",\"bucket\":\"energy\","
    "\"orgid\":\"0123456789abcdef\",\"authtoken\":\"") + std::string(88, 'A') + "\",\"postInterval\":10,\"bulksend\":1,"
    + HOSTILE_KEYS + ",\"outputs\":" + HOSTILE_OUT + "}";
  influxDB2_uploader* u = new influxDB2_uploader;
  TEST_ASSERT_TRUE(u->config(cfg.c_str()));
  char csv[200]; snprintf(csv, sizeof csv, ",result,table,_measurement,_time\r\n,_result,0,Main Panel,%u\r\n", T0 + 3600);
  hostHttpReplies.push_back({200, csv});
  hostHttpReplies.push_back({204, ""});
  runUntil(hostServices.back(), 2);
  TEST_ASSERT_EQUAL_INT(2, hostHttpLog.size());
  const std::string& q = hostHttpLog[0].body;
  TEST_ASSERT_TRUE_MESSAGE(q.find("from(bucket: \"energy\")") != std::string::npos, q.c_str());
  TEST_ASSERT_TRUE_MESSAGE(q.find("(r._measurement == \"Main Panel\" and r[\"loc=x\"] == \"O'Brien {a} \\\"q\\\"\" and r._field == \"kW,net\")") != std::string::npos, q.c_str());
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, hostHttpLog[1].body.rfind("Main\\ Panel,loc\\=x=O'Brien\\ {a}\\ \"q\" kW\\,net=", 0), hostHttpLog[1].body.c_str());
  delete u;
}

static void test_influx_escape_helpers(void){
  struct { const char *in, *flux, *ident, *str, *form, *key; } c[] = {
    {"${a}",      "\"\\${a}\"",  "\"${a}\"",   "'${a}'",   "%24%7Ba%7D",   "${a}"},
    {"a\\b",      "\"a\\\\b\"",  "\"a\\\\b\"", "'a\\\\b'", "a%5Cb",        "a\\b"},
    {"$x {y}",    "\"$x {y}\"",  "\"$x {y}\"", "'$x {y}'", "%24x+%7By%7D", "$x\\ {y}"},
    {"plain_1.2", "\"plain_1.2\"", "\"plain_1.2\"", "'plain_1.2'", "plain_1.2", "plain_1.2"},
    {"",          "\"\"",        "\"\"",       "''",       "",             ""},
  };
  for(auto& k : c){
    TEST_ASSERT_EQUAL_STRING_MESSAGE(k.flux,  fluxString(k.in).c_str(), k.in);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(k.ident, iqlIdent(k.in).c_str(), k.in);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(k.str,   iqlString(k.in).c_str(), k.in);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(k.form,  formEncode(k.in).c_str(), k.in);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(k.key,   lpKey(k.in).c_str(), k.in);
  }
  TEST_ASSERT_EQUAL_STRING("Main\\ Panel\\,x=y", lpMeasurement("Main Panel,x=y").c_str());   // '=' is literal in a measurement
  TEST_ASSERT_EQUAL_STRING("", lpKey(nullptr).c_str());
}

static std::string influxV1Config(int revision = 1, const char* extra = ""){
  return std::string("{\"type\":\"influxdb\",\"revision\":") + std::to_string(revision) + ",\"url\":\"http://influx.lan:8086\","
    "\"database\":\"iotawatt\",\"postInterval\":10,\"bulksend\":2,\"measurement\":\"$name\",\"fieldkey\":\"value\"" + extra + ",\"outputs\":" + OUTPUTS + "}";
}
static std::string influxSeries(const char* m, uint32_t t){
  char r[256]; snprintf(r, sizeof r, "{\"results\":[{\"statement_id\":0,\"series\":[{\"name\":\"%s\",\"columns\":[\"time\",\"last\"],\"values\":[[%u,1]]}]}]}", m, t);
  return r;
}
static bool logged(const char* text){ for(auto& l : hostLog) if(l.find(text) != std::string::npos) return true; return false; }

static void test_influxdb_v1_failures_and_retry(void){
  influxDB_uploader* u = new influxDB_uploader;
  TEST_ASSERT_TRUE(u->config(influxV1Config().c_str()));
  uint32_t resume = T0 + 3600;
  hostHttpReplies.push_back({500, "boom"});                 // query fails: retried after a delay
  runUntil(hostServices.back(), 1);
  for(int i = 0; i < 20; i++) hostServices.back()->service(hostServices.back());
  TEST_ASSERT_EQUAL_INT(1, hostHttpLog.size());               // still waiting out the delay
  hostUTC += 10;
  hostHttpReplies.push_back({200, influxSeries("Main", resume)});
  hostHttpReplies.push_back({200, influxSeries("Volts", resume)});
  hostHttpReplies.push_back({400, "bad line"});             // write rejected
  hostHttpReplies.push_back({204, ""});
  runUntil(hostServices.back(), 5);
  TEST_ASSERT_TRUE(hostHttpLog.size() >= 5);
  TEST_ASSERT_TRUE(hostHttpLog[3].url.find("/write") != std::string::npos);            // rejected with 400
  TEST_ASSERT_EQUAL_STRING(hostHttpLog[3].body.c_str(), hostHttpLog[4].body.c_str());   // the same data is resent
  delete u;
}

static void test_influxdb_v1_auth_failure_stops(void){
  influxDB_uploader* u = new influxDB_uploader;
  TEST_ASSERT_TRUE(u->config(influxV1Config().c_str()));
  hostHttpReplies.push_back({401, ""});
  runUntil(hostServices.back(), 2, 200);
  TEST_ASSERT_EQUAL_INT(1, hostHttpLog.size());               // no further requests
  TEST_ASSERT_TRUE(logged("Authentication failed"));
  delete u;
}

static void test_influxdb_v1_empty_database_starts_at_log_end(void){
  influxDB_uploader* u = new influxDB_uploader;
  TEST_ASSERT_TRUE(u->config(influxV1Config(1, ",\"begdate\":0").c_str()));
  hostHttpReplies.push_back({200, "{\"results\":[{\"statement_id\":0}]}"});
  hostHttpReplies.push_back({200, "{\"results\":[{\"statement_id\":0}]}"});
  runUntil(hostServices.back(), 3, 300);
  TEST_ASSERT_EQUAL_INT(2, hostHttpLog.size());               // nothing to write yet: starts at the log's end
  TEST_ASSERT_TRUE(logged("Start posting at"));
  delete u;
}

static void test_influxdb_v2_query_and_write(void){
  std::string token(88, 'A');
  std::string cfg = std::string("{\"type\":\"influxdb2\",\"revision\":1,\"url\":\"http://influx2.lan:8086\",\"bucket\":\"energy\","
    "\"orgid\":\"0123456789abcdef\",\"authtoken\":\"") + token + "\",\"postInterval\":10,\"bulksend\":1,"
    "\"measurement\":\"$name\",\"fieldkey\":\"value\",\"tagset\":[{\"key\":\"device\",\"value\":\"$device\"}],\"outputs\":" + OUTPUTS + "}";
  influxDB2_uploader* u = new influxDB2_uploader;
  TEST_ASSERT_TRUE(u->config(cfg.c_str()));
  uint32_t resume = T0 + 3600;
  char csv[200]; snprintf(csv, sizeof csv, ",result,table,_measurement,_time\r\n,_result,0,Main,%u\r\n", resume);
  hostHttpReplies.push_back({200, csv});
  hostHttpReplies.push_back({204, ""});
  runUntil(hostServices.back(), 2);
  TEST_ASSERT_EQUAL_INT(2, hostHttpLog.size());
  const HostHttpRequest& q = hostHttpLog[0];
  TEST_ASSERT_EQUAL_STRING("http://influx2.lan:8086/api/v2/query?orgID=0123456789abcdef", q.url.c_str());
  TEST_ASSERT_EQUAL_STRING(("Token " + token).c_str(), q.headers.at("Authorization").c_str());
  TEST_ASSERT_TRUE(q.body.find("from(bucket:\"energy\")") != std::string::npos || q.body.find("bucket: \"energy\"") != std::string::npos || q.body.find("energy") != std::string::npos);
  TEST_ASSERT_TRUE(q.body.find("r._measurement == \"Main\" and r[\"device\"] == \"IotaWatt\"") != std::string::npos);
  const HostHttpRequest& w = hostHttpLog[1];
  TEST_ASSERT_EQUAL_STRING("http://influx2.lan:8086/api/v2/write?precision=s&orgID=0123456789abcdef&bucket=energy", w.url.c_str());
  auto L = lines(w.body);
  char want[160]; uint32_t t = resume + 10;
  snprintf(want, sizeof want, "Main,device=IotaWatt value=%.2f %u", wattsAt(t), t);
  TEST_ASSERT_EQUAL_STRING(want, L[0].c_str());
  delete u;
}

// The shared influxBase pieces through both uploaders: the optional "heap" line (with the tag set),
// and, for v2, a URL given with its /api/v2 path.
static void test_influx_heap_line_both_versions(void){
  std::string token(88, 'A');
  for(int v2 = 0; v2 < 2; v2++){
    hostHttpLog.clear(); hostHttpReplies.clear(); hostServices.clear();
    std::string cfg = v2
      ? std::string("{\"type\":\"influxdb2\",\"revision\":1,\"url\":\"http://influx2.lan:8086/api/v2\",\"bucket\":\"energy\","
        "\"orgid\":\"0123456789abcdef\",\"authtoken\":\"") + token + "\","
      : std::string("{\"type\":\"influxdb\",\"revision\":1,\"url\":\"http://influx.lan:8086\",\"database\":\"iotawatt\",");
    cfg += std::string("\"postInterval\":10,\"bulksend\":1,\"heap\":true,\"measurement\":\"$name\",\"fieldkey\":\"value\","
      "\"tagset\":[{\"key\":\"device\",\"value\":\"$device\"}],\"outputs\":") + OUTPUTS + "}";
    Uploader* u = v2 ? (Uploader*)new influxDB2_uploader : (Uploader*)new influxDB_uploader;
    TEST_ASSERT_TRUE(u->config(cfg.c_str()));
    uint32_t resume = T0 + 3600;
    if(v2){
      char csv[200]; snprintf(csv, sizeof csv, ",result,table,_measurement,_time\r\n,_result,0,Main,%u\r\n", resume);
      hostHttpReplies.push_back({200, csv});
    } else {
      for(const char* m : {"Main", "Volts"}){
        char r[256]; snprintf(r, sizeof r, "{\"results\":[{\"statement_id\":0,\"series\":[{\"name\":\"%s\",\"columns\":[\"time\",\"last\"],\"values\":[[%u,1]]}]}]}", m, resume);
        hostHttpReplies.push_back({200, r});
      }
    }
    hostHttpReplies.push_back({204, ""});
    runUntil(hostServices.back(), v2 ? 2 : 3);
    const HostHttpRequest& w = hostHttpLog.back();
    TEST_ASSERT_EQUAL_STRING(v2 ? "http://influx2.lan:8086/api/v2/write?precision=s&orgID=0123456789abcdef&bucket=energy"
                                : "http://influx.lan:8086/write?precision=s&db=iotawatt", w.url.c_str());
    auto L = lines(w.body);
    TEST_ASSERT_TRUE(L.size() >= 3);
    char want[64]; snprintf(want, sizeof want, "heap,device=IotaWatt value=%u ", ESP.getFreeHeap());
    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE(want, L.back().c_str(), strlen(want), L.back().c_str());
    delete u;
  }
}

static void test_influxdb_v2_config_validation(void){
  const char* bad[] = {
    "\"bucket\":\"\",\"orgid\":\"0123456789abcdef\",\"authtoken\":\"T88\"",
    "\"orgid\":\"0123456789abcdef\",\"authtoken\":\"T88\"",
    "\"bucket\":\"b\",\"orgid\":\"short\",\"authtoken\":\"T88\"",
    "\"bucket\":\"b\",\"orgid\":\"0123456789abcdef\",\"authtoken\":\"tooshort\"",
  };
  std::string token(88, 'A');
  for(const char* b : bad){
    std::string fields = b; size_t at = fields.find("T88"); if(at != std::string::npos) fields.replace(at, 3, token);
    std::string cfg = "{\"type\":\"influxdb2\",\"revision\":1,\"url\":\"http://x:8086\",\"postInterval\":10," + fields + ",\"outputs\":" + OUTPUTS + "}";
    influxDB2_uploader* u = new influxDB2_uploader;
    TEST_ASSERT_FALSE_MESSAGE(u->config(cfg.c_str()), b);
    delete u;
  }
}

static std::string emoncmsConfig(const char* userid){
  return std::string("{\"type\":\"emoncms\",\"revision\":1,\"url\":\"http://emoncms.lan\",\"node\":\"house\","
    "\"apikey\":\"0123456789abcdef0123456789abcdef\",\"userid\":\"") + userid + "\",\"postInterval\":10,\"bulksend\":1,"
    "\"outputs\":[{\"name\":\"1\",\"units\":\"Watts\",\"script\":\"@1\"},{\"name\":\"3\",\"units\":\"Volts\",\"script\":\"@0\"}]}";
}

static void test_emoncms_plaintext(void){
  Emoncms_uploader* u = new Emoncms_uploader;
  TEST_ASSERT_TRUE(u->config(emoncmsConfig("").c_str()));
  uint32_t resume = T0 + 3600;
  char r[200]; snprintf(r, sizeof r, "{\"1\":{\"time\":%u,\"value\":100},\"3\":{\"time\":%u,\"value\":120}}", resume - 20, resume);
  hostHttpReplies.push_back({200, r});
  hostHttpReplies.push_back({200, "ok"});
  runUntil(hostServices.back(), 2);
  const HostHttpRequest& q = hostHttpLog[0];
  TEST_ASSERT_EQUAL_STRING("GET", q.method.c_str());
  TEST_ASSERT_EQUAL_STRING("http://emoncms.lan/input/get?node=house", q.url.c_str());
  TEST_ASSERT_EQUAL_STRING("Bearer 0123456789abcdef0123456789abcdef", q.headers.at("Authorization").c_str());
  const HostHttpRequest& w = hostHttpLog[1];
  TEST_ASSERT_EQUAL_STRING("http://emoncms.lan/input/bulk", w.url.c_str());
  // One post packs every pending interval up to the buffer limit: [dt,"node",output1,null,output3]
  // (output 2 isn't configured: null). Values trimmed of trailing zeros.
  char head[40]; snprintf(head, sizeof head, "time=%u&data=[", resume);
  TEST_ASSERT_TRUE(w.body.rfind(head, 0) == 0);
  size_t at = strlen(head); int frames = 0;
  while(at < w.body.size() && w.body[at] == '['){
    size_t end = w.body.find(']', at);
    uint32_t dt = 10 * (frames + 1);
    char want[64]; snprintf(want, sizeof want, "[%u,\"house\",%g,null,120]", dt, wattsAt(resume + dt));
    TEST_ASSERT_EQUAL_STRING(want, w.body.substr(at, end - at + 1).c_str());
    frames++; at = end + 1; if(w.body[at] == ',') at++;
  }
  TEST_ASSERT_TRUE(frames > 100);
  TEST_ASSERT_EQUAL_STRING("]", w.body.substr(at).c_str());
  delete u;
}

static void test_emoncms_encrypted(void){
  Emoncms_uploader* u = new Emoncms_uploader;
  TEST_ASSERT_TRUE(u->config(emoncmsConfig("42").c_str()));
  uint32_t resume = T0 + 3600;
  char r[120]; snprintf(r, sizeof r, "{\"1\":{\"time\":%u,\"value\":100}}", resume);
  hostHttpReplies.push_back({200, r});
  hostHttpReplies.push_back({200, EMONCMS_REPLY});
  runUntil(hostServices.back(), 2);
  const HostHttpRequest& w = hostHttpLog[1];
  TEST_ASSERT_EQUAL_STRING("aes128cbc", w.headers.at("content-type").c_str());
  TEST_ASSERT_EQUAL_STRING(EMONCMS_BODY, w.body.c_str());                          // from Python (golden)
  TEST_ASSERT_EQUAL_STRING(EMONCMS_AUTH, w.headers.at("Authorization").c_str());
  // the matching reply is accepted: the next write follows
  hostHttpReplies.push_back({200, "ok"});
  hostUTC += 100; runUntil(hostServices.back(), 3);
  TEST_ASSERT_EQUAL_INT(3, hostHttpLog.size());
  delete u;
}

static void test_base_config_validation(void){
  struct { const char* why; const char* json; } bad[] = {
    {"invalid url", "{\"revision\":1,\"url\":\"\",\"database\":\"d\",\"postInterval\":10,\"outputs\":[]}"},
    {"interval not a multiple of 5", "{\"revision\":1,\"url\":\"http://x\",\"database\":\"d\",\"postInterval\":7,\"outputs\":[]}"},
    {"no outputs", "{\"revision\":1,\"url\":\"http://x\",\"database\":\"d\",\"postInterval\":10}"},
    {"https without proxy", "{\"revision\":1,\"url\":\"https://x\",\"database\":\"d\",\"postInterval\":10,\"outputs\":[]}"},
    {"not json", "{\"revision\":1,"},
  };
  for(auto& b : bad){
    influxDB_uploader* u = new influxDB_uploader;
    TEST_ASSERT_FALSE_MESSAGE(u->config(b.json), b.why);
    delete u;
  }
}

static void test_revision_unchanged_is_a_no_op(void){
  influxDB_uploader* u = new influxDB_uploader;
  TEST_ASSERT_TRUE(u->config(influxV1Config(7).c_str()));
  size_t services = hostServices.size();
  TEST_ASSERT_TRUE(u->config(influxV1Config(7, ",\"database\":\"other\"").c_str()));   // same revision: ignored
  TEST_ASSERT_EQUAL_INT(services, hostServices.size());
  delete u;
}

static void test_no_wifi_no_requests(void){
  influxDB_uploader* u = new influxDB_uploader;
  TEST_ASSERT_TRUE(u->config(influxV1Config().c_str()));
  WiFi.connected = false;
  runUntil(hostServices.back(), 1, 200);
  TEST_ASSERT_EQUAL_INT(0, hostHttpLog.size());
  delete u;
}

static void test_status_json(void){
  influxDB_uploader* u = new influxDB_uploader;
  TEST_ASSERT_TRUE(u->config(influxV1Config().c_str()));
  hostHttpReplies.push_back({500, "down"});
  runUntil(hostServices.back(), 1);
  for(int i = 0; i < 5; i++) hostServices.back()->service(hostServices.back());
  JsonDocument d; JsonObject o = d.to<JsonObject>();
  u->getStatusJson(o);
  TEST_ASSERT_EQUAL_STRING("influxDB", o["id"].as<const char*>());
  TEST_ASSERT_EQUAL_STRING("running", o["status"].as<const char*>());
  TEST_ASSERT_TRUE(String(o["message"].as<const char*>()).startsWith("Query failed, code 500"));
  delete u;
}

// B47: an uploader that ends deletes itself through Uploader*; everything it allocated must be freed.
static void uploaderLifecycle(int kind){
  Uploader* u = kind == 0 ? (Uploader*)new influxDB_uploader : kind == 1 ? (Uploader*)new influxDB2_uploader : (Uploader*)new Emoncms_uploader;
  std::string token(88, 'A');
  std::string cfg = kind == 0 ? influxV1Config(1, ",\"user\":\"u\",\"pwd\":\"p\",\"retp\":\"rp\",\"tagset\":[{\"key\":\"a\",\"value\":\"b\"}]") :
                    kind == 1 ? std::string("{\"revision\":1,\"url\":\"http://x:8086\",\"bucket\":\"b\",\"orgid\":\"0123456789abcdef\",\"authtoken\":\"") + token +
                                "\",\"postInterval\":10,\"tagset\":[{\"key\":\"a\",\"value\":\"b\"}],\"outputs\":" + OUTPUTS + "}" :
                    emoncmsConfig("42");
  TEST_ASSERT_TRUE_MESSAGE(u->config(cfg.c_str()), cfg.c_str());
  serviceBlock* sb = hostServices.back();
  u->end();
  uint32_t r = 1; for(int i = 0; i < 50 && r; i++) r = sb->service(sb);      // stopped_s with _end: delete this
  TEST_ASSERT_EQUAL_UINT32(0, r);
  hostServices.pop_back(); delete sb;
  hostLog.clear(); hostHttpLog.clear(); hostHttpReplies.clear();   // the recorder's copies are the harness's memory
}

// B47/B51: an uploader that ends deletes itself through Uploader*; everything it allocated must be freed.
// One warm-up lifecycle per kind (one-time allocations), then five more must not grow the heap.
static void test_end_frees_everything(void){
  hostServices.reserve(256); hostLog.reserve(256); hostHttpLog.reserve(256);   // harness storage: not the firmware's
  for(int kind = 0; kind < 3; kind++){
    uploaderLifecycle(kind);
    long before = liveAllocs.load();
    for(int i = 0; i < 5; i++) uploaderLifecycle(kind);
    char msg[48]; snprintf(msg, sizeof msg, "uploader kind %d leaked", kind);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, (int32_t)(liveAllocs.load() - before), msg);
  }
}

// B65: a running uploader reconfigured without "outputs" frees them and stops; when it then ends, the
// destructor must not free them again (a double free since the destructors became virtual).
static void test_reconfig_without_outputs_then_end(void){
  for(int kind = 0; kind < 3; kind++){
    Uploader* u = kind == 0 ? (Uploader*)new influxDB_uploader : kind == 1 ? (Uploader*)new influxDB2_uploader : (Uploader*)new Emoncms_uploader;
    std::string token(88, 'A');
    std::string cfg = kind == 0 ? influxV1Config() :
                      kind == 1 ? std::string("{\"revision\":1,\"url\":\"http://x:8086\",\"bucket\":\"b\",\"orgid\":\"0123456789abcdef\",\"authtoken\":\"") + token +
                                  "\",\"postInterval\":10,\"outputs\":" + OUTPUTS + "}" :
                      emoncmsConfig("42");
    TEST_ASSERT_TRUE(u->config(cfg.c_str()));
    serviceBlock* sb = hostServices.back();
    std::string noOutputs = cfg;
    size_t at = noOutputs.find(",\"outputs\":");
    TEST_ASSERT_TRUE(at != std::string::npos);
    noOutputs.erase(at, noOutputs.find(']', at) + 1 - at);
    size_t rev = noOutputs.find("\"revision\":1");
    TEST_ASSERT_TRUE(rev != std::string::npos);
    noOutputs.replace(rev, 12, "\"revision\":2");
    TEST_ASSERT_FALSE_MESSAGE(u->config(noOutputs.c_str()), noOutputs.c_str());
    u->end();                                                   // what setConfig does next
    uint32_t r = 1; for(int i = 0; i < 50 && r; i++) r = sb->service(sb);
    TEST_ASSERT_EQUAL_UINT32(0, r);
    hostServices.pop_back(); delete sb;
    hostLog.clear(); hostHttpLog.clear(); hostHttpReplies.clear();
  }
}

// B66: each new revision of an encrypted Emoncms config (with a userid) must not leak the hash buffer.
static void test_emoncms_reconfig_no_leak(void){
  Emoncms_uploader* u = new Emoncms_uploader;
  std::string cfg = emoncmsConfig("42");
  size_t rev = cfg.find("\"revision\":1");
  TEST_ASSERT_TRUE(rev != std::string::npos);
  auto withRevision = [&](int r){ std::string c = cfg; c.replace(rev, 12, "\"revision\":" + std::to_string(r)); return c; };
  TEST_ASSERT_TRUE(u->config(withRevision(1).c_str()));
  hostServices.back()->service(hostServices.back());            // running, as on the device (B100 otherwise)
  TEST_ASSERT_TRUE(u->config(withRevision(2).c_str()));
  long before = liveAllocs.load();
  for(int r = 3; r < 8; r++) TEST_ASSERT_TRUE(u->config(withRevision(r).c_str()));
  TEST_ASSERT_EQUAL_INT32(0, (int32_t)(liveAllocs.load() - before));
  delete u;
  for(auto* b : hostServices) delete b;
  hostServices.clear(); hostLog.clear();
}

// Bad server replies: random codes and bodies must not crash or wedge the uploaders.
static void test_fuzz_server_replies(void){
  uint32_t seed = 77;
  auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
  const char* bodies[] = {"", "ok", "{", "{\"results\":[]}", "{\"results\":[{\"series\":[{\"columns\":[\"time\"],\"values\":[[]]}]}]}",
    "{\"results\":[{\"series\":[{\"columns\":[1],\"values\":[[\"x\"]]}]}]}", "\"Node does not exist\"", "{\"1\":{\"time\":\"x\"}}",
    ",result\r\n", ",_time\r\n,abc\r\n", "\r\n\r\n", "null", "[1,2,3]"};
  const int codes[] = {-4, -1, 0, 200, 204, 301, 400, 401, 404, 500};
  std::string token(88, 'A');
  for(int round = 0; round < 60; round++){
    setUp();
    int kind = round % 3;
    Uploader* u = kind == 0 ? (Uploader*)new influxDB_uploader : kind == 1 ? (Uploader*)new influxDB2_uploader : (Uploader*)new Emoncms_uploader;
    std::string cfg = kind == 0 ? influxV1Config() :
                      kind == 1 ? std::string("{\"revision\":1,\"url\":\"http://x:8086\",\"bucket\":\"b\",\"orgid\":\"0123456789abcdef\",\"authtoken\":\"") + token +
                                  "\",\"postInterval\":10,\"outputs\":" + OUTPUTS + "}" : emoncmsConfig(round % 2 ? "42" : "");
    TEST_ASSERT_TRUE(u->config(cfg.c_str()));
    for(int i = 0; i < 40; i++) hostHttpReplies.push_back({codes[rnd() % 10], bodies[rnd() % 13]});
    serviceBlock* sb = hostServices.back();
    for(int i = 0; i < 3000; i++){ sb->service(sb); if(i % 50 == 0) hostUTC += 30; }
    delete u;
  }
}

int main(int, char**){
  setenv("TZ", "UTC", 1); tzset();
  struct tm t{}; t.tm_year = 2026 - 1900; t.tm_mon = 10; t.tm_mday = 23; T0 = (uint32_t)timegm(&t);
  for(int i = 0; i < 2; i++){ chans[i] = new IotaInputChannel(i); chans[i]->_vmult = 1; chans[i]->active(true); }
  chans[0]->_type = channelTypeVoltage; chans[0]->_name = charstar("Volts");
  chans[1]->_type = channelTypePower;   chans[1]->_name = charstar("Main");
  inputChannel = chans; maxInputs = 2;
  buildLog(3);
  UNITY_BEGIN();
  RUN_TEST(test_influxdb_v1_query_and_write);
  RUN_TEST(test_influx_escaping_v1);
  RUN_TEST(test_influx_escaping_v2);
  RUN_TEST(test_influx_escape_helpers);
  RUN_TEST(test_influxdb_v1_failures_and_retry);
  RUN_TEST(test_influxdb_v1_auth_failure_stops);
  RUN_TEST(test_influxdb_v1_empty_database_starts_at_log_end);
  RUN_TEST(test_influxdb_v2_query_and_write);
  RUN_TEST(test_influx_heap_line_both_versions);
  RUN_TEST(test_influxdb_v2_config_validation);
  RUN_TEST(test_emoncms_plaintext);
  RUN_TEST(test_emoncms_encrypted);
  RUN_TEST(test_base_config_validation);
  RUN_TEST(test_revision_unchanged_is_a_no_op);
  RUN_TEST(test_no_wifi_no_requests);
  RUN_TEST(test_status_json);
  RUN_TEST(test_end_frees_everything);
  RUN_TEST(test_reconfig_without_outputs_then_end);
  RUN_TEST(test_emoncms_reconfig_no_leak);
  RUN_TEST(test_fuzz_server_replies);
  return UNITY_END();
}
