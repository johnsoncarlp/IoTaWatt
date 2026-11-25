// /query (CSVquery): argument parsing, grouping, units, formats, limits and the time budget, run on a
// real IotaLog filled with synthetic data whose every grouped value is known.
#include <unity.h>
#include "host_query_deps.h"
#include "host_xbuf.h"
#include "IotaLog.cpp"
#include "IotaScript.cpp"
#include "timeRules.cpp"
#include "CSVquery.cpp"

static uint32_t T0;                                        // 2026-11-23 00:00 UTC
static const int DAYS = 2;
static IotaInputChannel* chans[2];
static double wattsAt(uint32_t t){ return 100.0 * (1 + ((t - T0) / 3600) % 24); }   // changes every hour

static void buildLog(){
  SD.reset();
  Current_log.~IotaLog(); new (&Current_log) IotaLog(256, 5, 365, 24);
  Current_log.begin("/iotawatt/iotalog.log");
  IotaLogRecord r; double wh = 0, vh = 0, hzh = 0, hours = 0;
  for(uint32_t t = T0; t <= T0 + DAYS * 86400; t += 5){
    if(t > T0){ double dh = 5.0 / 3600; hours += dh; wh += wattsAt(t - 5) * dh; vh += 120.0 * dh; hzh += 60.0 * dh; }
    r.UNIXtime = t; r.logHours = hours;
    r.accum1[0] = vh; r.accum2[0] = hzh;                    // VT: volt-hours, Hz-hours
    r.accum1[1] = wh; r.accum2[1] = wh / 0.8;               // CT: Wh, VAh (PF 0.8)
    Current_log.write(&r);
  }
}

void setUp(void){
  server.args.clear(); hostLog.clear(); hostMillis = 1000;
  localTimeDiff = 0; hostLocalOffset = 0; timezoneRule = nullptr;
  hostUTC = T0 + DAYS * 86400; hostLocalTime = hostUTC;
}
void tearDown(void){}

static std::string run(CSVquery& q){
  std::string out; uint8_t buf[1440]; size_t n; int guard = 0;
  // Each buffer takes time on the device: advance millis() so the 60 s budget (B27) behaves as there.
  while((n = q.readResult(buf, sizeof buf)) && guard++ < 20000){ out.append((char*)buf, n); hostMillis += 10; }
  if(guard >= 20000){
    std::string a; for(auto& kv : server.args) a += kv.first + "=" + kv.second + " ";
    printf("did not terminate: %s\n  output starts: %.120s\n", a.c_str(), out.c_str());
  }
  TEST_ASSERT_TRUE_MESSAGE(guard < 20000, "readResult did not terminate");
  return out;
}
static std::string query(std::initializer_list<std::pair<const char*, std::string>> a, bool expectOk = true){
  server.args.clear(); for(auto& kv : a) server.args[kv.first] = kv.second;
  CSVquery q;
  bool ok = q.setup();
  TEST_ASSERT_EQUAL_MESSAGE(expectOk, ok, q.failReason().c_str());
  return ok ? run(q) : std::string("FAIL:") + q.failReason().c_str();
}
static std::string U(uint32_t t){ return std::to_string(t); }

static void test_hourly_json_with_header(void){
  std::string out = query({{"select", "[time.utc.unix,Main.watts,Volts.volts]"}, {"begin", U(T0)}, {"end", U(T0 + 86400)},
                           {"group", "1h"}, {"format", "json"}, {"header", "yes"}});
  JsonDocument d; TEST_ASSERT_FALSE_MESSAGE(deserializeJson(d, out), out.c_str());
  TEST_ASSERT_EQUAL_UINT32(T0, d["range"][0].as<uint32_t>());
  TEST_ASSERT_EQUAL_UINT32(T0 + 86400, d["range"][1].as<uint32_t>());
  TEST_ASSERT_EQUAL_STRING("Main", d["labels"][1].as<const char*>());
  JsonArray data = d["data"];
  TEST_ASSERT_EQUAL_INT(24, data.size());
  for(int h = 0; h < 24; h++){
    TEST_ASSERT_EQUAL_UINT32(T0 + 3600 * h, data[h][0].as<uint32_t>());
    TEST_ASSERT_DOUBLE_WITHIN(0.05, 100.0 * (h + 1), data[h][1].as<double>());
    TEST_ASSERT_DOUBLE_WITHIN(0.05, 120.0, data[h][2].as<double>());
  }
}

static void test_units_and_decimals(void){
  std::string out = query({{"select", "[Main.wh,Main.va,Main.pf,Main.amps.d3,Volts.hz,Total]"}, {"begin", U(T0 + 7200)},
                           {"end", U(T0 + 10800)}, {"group", "1h"}, {"format", "json"}});
  JsonDocument d; TEST_ASSERT_FALSE_MESSAGE(deserializeJson(d, out), out.c_str());
  JsonArray row = d[0];
  TEST_ASSERT_DOUBLE_WITHIN(0.5, 300.0, row[0].as<double>());            // Wh in the hour 02:00-03:00
  TEST_ASSERT_DOUBLE_WITHIN(0.05, 375.0, row[1].as<double>());           // VA = W / 0.8
  TEST_ASSERT_DOUBLE_WITHIN(0.05, 0.8, row[2].as<double>());
  TEST_ASSERT_DOUBLE_WITHIN(0.0005, 375.0 / 120.0, row[3].as<double>()); // .d3: three decimals
  TEST_ASSERT_DOUBLE_WITHIN(0.005, 60.0, row[4].as<double>());
  TEST_ASSERT_DOUBLE_WITHIN(0.05, 300.0, row[5].as<double>());           // output script @1
}

static void test_csv_daily_and_raw(void){
  std::string csv = query({{"select", "[time.utc.iso,Main.watts]"}, {"begin", U(T0)}, {"end", U(T0 + 2 * 86400)},
                           {"group", "1d"}, {"format", "csv"}, {"header", "yes"}});
  TEST_ASSERT_EQUAL_STRING("Time, Main\r\n2026-11-23T00:00:00, 1250\r\n2026-11-24T00:00:00, 1250", csv.c_str());
  std::string raw = query({{"select", "[time.utc.unix,Main.watts]"}, {"begin", U(T0 + 3600)}, {"end", U(T0 + 3660)},
                           {"group", "5s"}, {"format", "json"}});
  JsonDocument d; TEST_ASSERT_FALSE(deserializeJson(d, raw));
  TEST_ASSERT_EQUAL_INT(12, d.size());
  TEST_ASSERT_DOUBLE_WITHIN(0.05, 200.0, d[0][1].as<double>());
}

static void test_group_auto(void){
  std::string out = query({{"select", "[time.utc.unix]"}, {"begin", U(T0)}, {"end", U(T0 + 2 * 86400)}, {"group", "auto"}, {"format", "json"}});
  JsonDocument d; TEST_ASSERT_FALSE(deserializeJson(d, out));
  TEST_ASSERT_EQUAL_INT(288, d.size());                                   // 172800 s / 400 -> 600 s groups
  TEST_ASSERT_EQUAL_UINT32(600, d[1][0].as<uint32_t>() - d[0][0].as<uint32_t>());
}

static void test_dates_and_relative_times(void){
  struct { const char* begin; const char* end; uint32_t b, e; } c[] = {
    {"2026-11-23", "2026-11-24", T0, T0 + 86400},
    {"2026-11-23T01:00:00", "2026-11-23T03:00:00Z", T0 + 3600, T0 + 3 * 3600},
    {"d-1d", "d", T0 + 86400, T0 + 2 * 86400},                            // yesterday (now = T0 + 2 days)
    {"s-2h", "s", T0 + 2 * 86400 - 7200, T0 + 2 * 86400},
  };
  for(auto& x : c){
    std::string out = query({{"select", "[time.utc.unix]"}, {"begin", x.begin}, {"end", x.end}, {"group", "1h"},
                             {"format", "json"}, {"header", "yes"}});
    JsonDocument d; TEST_ASSERT_FALSE_MESSAGE(deserializeJson(d, out), x.begin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(x.b, d["range"][0].as<uint32_t>(), x.begin);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(x.e, d["range"][1].as<uint32_t>(), x.end);
  }
}

static void test_limit_marker(void){
  std::string out = query({{"select", "[time.utc.unix,Main.watts]"}, {"begin", U(T0)}, {"end", U(T0 + 86400)},
                           {"group", "1h"}, {"format", "json"}, {"header", "yes"}, {"limit", "10"}});
  JsonDocument d; TEST_ASSERT_FALSE_MESSAGE(deserializeJson(d, out), out.c_str());
  TEST_ASSERT_EQUAL_INT(10, d["data"].size());
  TEST_ASSERT_EQUAL_UINT32(T0 + 36000, d["limit"].as<uint32_t>());
}

// B27: a query that runs past the time budget ends with the "limit" marker (CSV, JSON with header),
// or aborts the transfer (headerless JSON, which has nowhere to put a marker).
static void test_time_budget(void){
  for(int mode = 0; mode < 3; mode++){
    server.args = {{"select", "[time.utc.unix,Main.watts]"}, {"begin", U(T0)}, {"end", U(T0 + 86400)}, {"group", "1h"},
                   {"limit", "none"}, {"format", mode == 0 ? "csv" : "json"}, {"header", mode == 2 ? "no" : "yes"}};
    CSVquery q; TEST_ASSERT_TRUE(q.setup());
    hostMillis += 61000;                                                   // past the 60 s budget
    std::string out = run(q);
    if(mode == 0) TEST_ASSERT_TRUE_MESSAGE(out.find("Limit exceeded at ") != std::string::npos && out.find(" (time limit)") != std::string::npos, out.c_str());   // the row-limit prefix clients match
    if(mode == 1){ JsonDocument d; TEST_ASSERT_FALSE(deserializeJson(d, out)); TEST_ASSERT_TRUE(d["timeout"].as<bool>()); }
    if(mode == 2){ TEST_ASSERT_TRUE(q.timedOut()); JsonDocument d; TEST_ASSERT_TRUE((bool)deserializeJson(d, out)); }   // incomplete
  }
}

static void test_missing_data_modes(void){
  for(const char* m : {"null", "zero", "skip"}){
    std::string out = query({{"select", "[time.utc.unix,Main.watts]"}, {"begin", U(T0 - 7200)}, {"end", U(T0 + 3600)},
                             {"group", "1h"}, {"format", "json"}, {"missing", m}});
    JsonDocument d; TEST_ASSERT_FALSE_MESSAGE(deserializeJson(d, out), out.c_str());
    if(!strcmp(m, "skip")) TEST_ASSERT_EQUAL_INT_MESSAGE(1, d.size(), m);
    else { TEST_ASSERT_EQUAL_INT_MESSAGE(3, d.size(), m);
           if(!strcmp(m, "null")) TEST_ASSERT_TRUE(d[0][1].isNull()); else TEST_ASSERT_EQUAL_INT(0, d[0][1].as<int>()); }
  }
}

static void test_show_series(void){
  server.args = {{"show", "series"}};
  CSVquery q; TEST_ASSERT_TRUE(q.setup());
  std::string out = run(q);
  JsonDocument d; TEST_ASSERT_FALSE_MESSAGE(deserializeJson(d, out), out.c_str());
  TEST_ASSERT_TRUE(out.find("\"Main\"") != std::string::npos);
  TEST_ASSERT_TRUE(out.find("\"Total\"") != std::string::npos);
}

static void test_invalid_requests(void){
  const std::string b = U(T0), e = U(T0 + 3600);
  query({{"select", "[time]"}, {"end", e}, {"group", "1h"}}, false);                                  // no begin
  query({{"select", "[time]"}, {"begin", e}, {"end", b}, {"group", "1h"}}, false);                    // end < begin
  query({{"select", "[time]"}, {"begin", b}, {"end", e}, {"group", "7"}}, false);                     // not a multiple of 5
  query({{"select", "[time]"}, {"begin", b}, {"end", e}, {"group", "1h"}, {"format", "xml"}}, false);
  query({{"select", "[time]"}, {"begin", b}, {"end", e}, {"group", "1h"}, {"limit", "0"}}, false);
  query({{"select", "time"}, {"begin", b}, {"end", e}, {"group", "1h"}}, false);                      // not an array
  query({{"select", "[nosuch]"}, {"begin", b}, {"end", e}, {"group", "1h"}}, false);
  query({{"select", "[Main.furlongs]"}, {"begin", b}, {"end", e}, {"group", "1h"}}, false);
  std::string r = query({{"select", "[]"}, {"begin", b}, {"end", e}, {"group", "1h"}}, false);       // B4
  TEST_ASSERT_TRUE(r.find("No series") != std::string::npos);
  query({{"select", "[,time]"}, {"begin", b}, {"end", e}, {"group", "1h"}, {"format", "json"}});      // B4: empty element dropped
  query({{"select", "[time]"}, {"begin", U(T0 - 7200)}, {"end", e}, {"group", "1h"}, {"missing", "skip"},
         {"format", "json"}});                                                                       // B16: terminates
  query({{"select", "[time]"}, {"begin", "2026-13-01"}, {"end", e}, {"group", "1h"}}, false);
  query({{"select", "[time]"}, {"begin", "d+0d"}, {"end", e}, {"group", "1h"}}, false);
}

// A zero-length group must not loop: "all" over an empty range, and groups that don't advance.
static void test_groups_always_advance(void){
  for(const char* g : {"all", "1h", "5s"}){
    server.args = {{"select", "[time.utc.unix,Main.watts]"}, {"begin", U(T0 + 3600)}, {"end", U(T0 + 3600)},
                   {"group", g}, {"format", "json"}, {"limit", "none"}};
    CSVquery q;
    if(!q.setup()) continue;
    uint32_t before = hostMillis;
    std::string out; uint8_t buf[1440]; size_t n; int calls = 0;
    while((n = q.readResult(buf, sizeof buf)) && calls++ < 1000) out.append((char*)buf, n);   // no millis: no budget
    TEST_ASSERT_TRUE_MESSAGE(calls < 1000, g);
    TEST_ASSERT_EQUAL_UINT32(before, hostMillis);
  }
}

static void test_fuzz_random_arguments(void){
  uint32_t seed = 515;
  auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
  const char* sel[] = {"[time]", "[Main]", "[,]", "[", "[Main.watts.d9,Volts.hz]", "[time.iso.local,Total.wh]", "[a.b.c]", "[]", "[[Main]]"};
  const char* tm[] = {"d", "s", "s-1h", "M-1M", "y", "w-2w", "2026-11-23", "2026-11-23T25:00:00", "1795392000", "1795392000000",
                      "x", "", "d-99999d", "2099-12-31T23:59:59Z", "s+5s", "-1", "2000"};
  const char* grp[] = {"1h", "5s", "auto", "all", "1M", "1y", "3w", "0", "x", "10", "1m", "-5"};
  const char* fmt[] = {"json", "csv", "CSV", "bad"};
  const char* mis[] = {"null", "zero", "skip", "x"};
  for(int iter = 0; iter < 1500; iter++){
    server.args.clear();
    server.args["select"] = sel[rnd() % 9]; server.args["begin"] = tm[rnd() % 17]; server.args["end"] = tm[rnd() % 17];
    server.args["group"] = grp[rnd() % 12]; server.args["format"] = fmt[rnd() % 4];
    if(rnd() % 2) server.args["missing"] = mis[rnd() % 4];
    if(rnd() % 2) server.args["header"] = (rnd() % 2) ? "yes" : "no";
    if(rnd() % 3 == 0) server.args["limit"] = (rnd() % 2) ? "none" : std::to_string(1 + rnd() % 50);
    CSVquery q;
    if(q.setup()) run(q);
  }
}

int main(int, char**){
  setenv("TZ", "UTC", 1); tzset();                        // mktime() as on the device (no TZ: UTC)
  struct tm t{}; t.tm_year = 2026 - 1900; t.tm_mon = 10; t.tm_mday = 23; T0 = (uint32_t)timegm(&t);
  for(int i = 0; i < 2; i++){ chans[i] = new IotaInputChannel(i); chans[i]->_vmult = 1; chans[i]->active(true); }
  chans[0]->_type = channelTypeVoltage; chans[0]->_name = charstar("Volts");
  chans[1]->_type = channelTypePower;   chans[1]->_name = charstar("Main");
  inputChannel = chans; maxInputs = 2;
  JsonDocument od; deserializeJson(od, "[{\"name\":\"Total\",\"units\":\"Watts\",\"script\":\"@1\"}]");
  delete outputs; outputs = new ScriptSet(od.as<JsonArray>());
  buildLog();
  UNITY_BEGIN();
  RUN_TEST(test_hourly_json_with_header);
  RUN_TEST(test_units_and_decimals);
  RUN_TEST(test_csv_daily_and_raw);
  RUN_TEST(test_group_auto);
  RUN_TEST(test_dates_and_relative_times);
  RUN_TEST(test_limit_marker);
  RUN_TEST(test_time_budget);
  RUN_TEST(test_missing_data_modes);
  RUN_TEST(test_show_series);
  RUN_TEST(test_invalid_requests);
  RUN_TEST(test_groups_always_advance);
  RUN_TEST(test_fuzz_random_arguments);
  return UNITY_END();
}
