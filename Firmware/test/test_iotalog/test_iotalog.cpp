// IotaLog: the fixed-record, keyed, wrapping log behind the current/history datalogs and integrations.
// Every test checks the real IotaLog against an independent model of what was written.
#include <unity.h>
#include "host_iotawatt.h"
#include "IotaLog.cpp"

static const char* PATH = "/iotawatt/test.log";
static const uint32_t T0 = 1795560000;          // 2026-11-24, a multiple of 5
static const uint16_t RS = 32;                  // record size (integration logs); 720 records = 1 hour min size

struct ModelRec { uint32_t key; int32_t serial; };

// Write a record with key and an identifying logHours; returns IotaLog's result.
static int put(IotaLog& log, uint32_t key){
  IotaLogRecord r; r.UNIXtime = key; r.logHours = key / 5.0;
  return log.write(&r);
}

// readKey and check it against the model: the record with the largest key <= k (or the first record),
// returned with UNIXtime set to k rounded down to the interval.
static void checkRead(IotaLog& log, const std::vector<ModelRec>& m, uint32_t k, uint16_t interval, const char* where){
  IotaLogRecord r; r.UNIXtime = k;
  int rc = log.readKey(&r);
  uint32_t kk = k - k % interval;
  char msg[160];
  snprintf(msg, sizeof msg, "%s: key %u (first %u last %u)", where, kk, m.front().key, m.back().key);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(kk, r.UNIXtime, msg);
  if(kk < m.front().key){
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, rc, msg);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(m.front().serial, r.serial, msg);
    return;
  }
  auto it = std::upper_bound(m.begin(), m.end(), kk, [](uint32_t v, const ModelRec& x){ return v < x.key; });
  const ModelRec& want = *(it - 1);
  TEST_ASSERT_EQUAL_INT_MESSAGE(kk > m.back().key ? 1 : 0, rc, msg);          // B17: past the end is 1
  TEST_ASSERT_EQUAL_INT32_MESSAGE(want.serial, r.serial, msg);
  TEST_ASSERT_EQUAL_DOUBLE_MESSAGE(want.key / 5.0, r.logHours, msg);
}

static void checkBounds(IotaLog& log, const std::vector<ModelRec>& m, const char* where){
  char msg[96]; snprintf(msg, sizeof msg, "%s: bounds", where);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(m.front().key, log.firstKey(), msg);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(m.back().key, log.lastKey(), msg);
  TEST_ASSERT_EQUAL_INT32_MESSAGE(m.front().serial, log.firstSerial(), msg);
  TEST_ASSERT_EQUAL_INT32_MESSAGE(m.back().serial, log.lastSerial(), msg);
}

void setUp(void){ SD.reset(); hostLog.clear(); ESP.restarts = 0; }
void tearDown(void){}

static void test_new_log_is_empty(void){
  IotaLog log(RS, 5, 1, 0);
  TEST_ASSERT_EQUAL_INT(0, log.begin(PATH));
  TEST_ASSERT_TRUE(log.isOpen());
  TEST_ASSERT_TRUE(SD.exists("/iotawatt"));
  IotaLogRecord r; r.UNIXtime = T0;
  TEST_ASSERT_EQUAL_INT(1, log.readKey(&r));
  TEST_ASSERT_EQUAL_INT(0, log.begin(PATH));     // second begin is a no-op
}

static void test_sequential_write_and_keyed_reads(void){
  IotaLog log(RS, 5, 1, 24);
  log.begin(PATH);
  std::vector<ModelRec> m;
  for(int i = 0; i < 600; i++){
    TEST_ASSERT_EQUAL_INT(0, put(log, T0 + 5 * i));
    m.push_back({T0 + 5 * (uint32_t)i, i});
  }
  checkBounds(log, m, "sequential");
  for(uint32_t k = T0 - 20; k < T0 + 5 * 600 + 20; k += 1) checkRead(log, m, k, 5, "sequential");
}

static void test_holes_return_previous_record(void){
  IotaLog log(RS, 5, 1, 0);
  log.begin(PATH);
  std::vector<ModelRec> m; uint32_t k = T0; int32_t s = 0;
  for(int i = 0; i < 200; i++){
    put(log, k); m.push_back({k, s++});
    k += 5 * (1 + (i % 7 == 0 ? 37 : 0) + (i % 13 == 0 ? 3 : 0));      // gaps of various lengths
  }
  for(uint32_t q = T0; q <= m.back().key + 10; q += 5) checkRead(log, m, q, 5, "holes");
}

static void test_write_rejects_non_increasing_keys(void){
  IotaLog log(RS, 5, 1, 0);
  log.begin(PATH);
  TEST_ASSERT_EQUAL_INT(0, put(log, T0 + 100));
  TEST_ASSERT_EQUAL_INT(1, put(log, T0 + 100));      // same key
  TEST_ASSERT_EQUAL_INT(1, put(log, T0 + 50));       // older key
  TEST_ASSERT_EQUAL_INT(0, put(log, T0 + 105));
  TEST_ASSERT_EQUAL_INT32(1, log.lastSerial());
}

// Wrap: the file stops growing at max size and overwrites the oldest record.
static void test_wrap_keeps_bounds_and_reads_exact(void){
  IotaLog log(RS, 5, 0, 0);                        // 0 days -> minimum: 1 hour = 720 records
  log.begin(PATH);
  std::vector<ModelRec> all;
  const uint32_t cap = 720;
  for(uint32_t i = 0; i < cap * 3 + 17; i++){
    TEST_ASSERT_EQUAL_INT(0, put(log, T0 + 5 * i));
    all.push_back({T0 + 5 * i, (int32_t)i});
    std::vector<ModelRec> m(all.end() - std::min<size_t>(all.size(), cap), all.end());
    // B25: after the write that lands on the last physical record, firstKey must not equal lastKey.
    char where[48]; snprintf(where, sizeof where, "after write %u", i);
    checkBounds(log, m, where);
  }
  TEST_ASSERT_EQUAL_UINT32(cap * RS, log.fileSize());
  std::vector<ModelRec> m(all.end() - cap, all.end());
  for(uint32_t k = all.front().key; k <= all.back().key + 10; k += 5) checkRead(log, m, k, 5, "wrapped");
}

// Reopen: a new IotaLog on the same file reconstructs first/last and the wrap point (findWrap).
static void test_reopen_reconstructs_state(void){
  const uint32_t cap = 720;
  for(uint32_t total : {1u, 2u, 500u, cap - 1, cap, cap + 1, cap + 359, 2 * cap - 1, 2 * cap, 5 * cap + 123}){
    SD.reset();
    std::vector<ModelRec> all;
    {
      IotaLog log(RS, 5, 0, 24);
      log.begin(PATH);
      for(uint32_t i = 0; i < total; i++){ put(log, T0 + 5 * i); all.push_back({T0 + 5 * i, (int32_t)i}); }
      log.end();
    }
    IotaLog again(RS, 5, 0, 24);
    TEST_ASSERT_EQUAL_INT(0, again.begin(PATH));
    TEST_ASSERT_EQUAL_INT(0, ESP.restarts);
    std::vector<ModelRec> m(all.end() - std::min<size_t>(all.size(), cap), all.end());
    char where[48]; snprintf(where, sizeof where, "reopen after %u", total);
    checkBounds(again, m, where);
    for(uint32_t k = m.front().key; k <= m.back().key; k += 5 * 7) checkRead(again, m, k, 5, where);
    // and it keeps writing correctly after reopening
    uint32_t next = all.back().key + 5;
    TEST_ASSERT_EQUAL_INT(0, put(again, next));
    all.push_back({next, (int32_t)total});
    std::vector<ModelRec> m2(all.end() - std::min<size_t>(all.size(), cap), all.end());
    checkBounds(again, m2, where);
  }
}

static void test_write_cache_matches_direct_writes(void){
  IotaLog log(RS, 5, 1, 0);
  log.begin(PATH);
  std::vector<ModelRec> m;
  for(int i = 0; i < 50; i++){ put(log, T0 + 5 * i); m.push_back({T0 + 5 * (uint32_t)i, i}); }
  log.writeCache(true);
  for(int i = 50; i < 333; i++){
    put(log, T0 + 5 * i); m.push_back({T0 + 5 * (uint32_t)i, i});
    checkRead(log, m, T0 + 5 * i, 5, "cached");                 // reads inside the write cache
  }
  log.writeCache(false);
  for(uint32_t k = T0; k <= m.back().key; k += 5) checkRead(log, m, k, 5, "after cache");
  log.end();
  IotaLog again(RS, 5, 1, 0);
  again.begin(PATH);
  checkBounds(again, m, "cache reopen");
}

// Bad input: a record whose serial doesn't follow is the device's "file damaged" path: it logs, writes a
// diagnostic file, deletes the log and restarts.
static void test_damaged_file_is_detected(void){
  {
    IotaLog log(RS, 5, 1, 0);
    log.begin(PATH);
    for(int i = 0; i < 100; i++) put(log, T0 + 5 * i);
    log.end();
  }
  int32_t bogus = 9999;                                         // corrupt the last record's serial
  SD.data(PATH)->writeAt(99 * RS + 4, (const uint8_t*)&bogus, 4);
  IotaLog again(RS, 5, 1, 0);
  again.begin(PATH);
  TEST_ASSERT_EQUAL_INT(1, ESP.restarts);
  TEST_ASSERT_FALSE(SD.exists(PATH));
  TEST_ASSERT_TRUE(SD.exists("/iotaWatt/logDiag.txt"));
  bool logged = false; for(auto& l : hostLog) logged |= l.find("file damaged") != std::string::npos;
  TEST_ASSERT_TRUE(logged);
}

// Bad input: a file that isn't a whole number of records (e.g. power lost mid-write) must not crash.
static void test_truncated_file_does_not_crash(void){
  {
    IotaLog log(RS, 5, 1, 0);
    log.begin(PATH);
    for(int i = 0; i < 40; i++) put(log, T0 + 5 * i);
    log.end();
  }
  SD.data(PATH)->size -= 7;
  IotaLog again(RS, 5, 1, 0);
  again.begin(PATH);                                            // damaged-or-not is fine; no crash/UB
  IotaLogRecord r; r.UNIXtime = T0 + 50;
  again.readKey(&r);
}

// Bad input: random workload: random gaps, reopens and lookups, against the model.
static void test_fuzz_random_workload(void){
  uint32_t seed = 4242;
  auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
  const uint32_t cap = 720;
  std::vector<ModelRec> all; uint32_t k = T0; int32_t s = 0;
  IotaLog* log = new IotaLog(RS, 5, 0, 24);
  log->begin(PATH);
  for(int step = 0; step < 6000; step++){
    int op = rnd() % 100;
    if(op < 70){
      k += 5 * (1 + ((rnd() % 10 == 0) ? rnd() % 50 : 0));
      TEST_ASSERT_EQUAL_INT(0, put(*log, k)); all.push_back({k, s++});
    } else if(op < 72){
      log->end(); delete log;
      log = new IotaLog(RS, 5, 0, 24);
      TEST_ASSERT_EQUAL_INT(0, log->begin(PATH));
      TEST_ASSERT_EQUAL_INT(0, ESP.restarts);
    } else if(!all.empty()){
      std::vector<ModelRec> m(all.end() - std::min<size_t>(all.size(), cap), all.end());
      uint32_t span = m.back().key - m.front().key + 200;
      checkRead(*log, m, m.front().key - 100 + (uint32_t)(((uint64_t)rnd() << 15 | rnd()) % span), 5, "fuzz");
    }
  }
  delete log;
}

// B25: a wrapped log larger than 2 GB. The file is synthesized (sparse, generated on read), so this
// needs no memory: 15,000,000 records of 256 bytes = 3.84 GB, wrap point near the end so that
// offset + wrap passes 4 GB for most of the log.
static void test_huge_wrapped_log_over_2GB(void){
  const uint32_t N = 15000000, W = N - 1000;                    // physical index of the oldest record
  const int32_t S0 = 1000;
  auto d = SD.create(PATH);
  d->size = (uint64_t)N * 256;
  d->generator = [=](uint64_t pos, uint8_t* out, size_t len){
    for(size_t i = 0; i < len; i++){
      uint64_t p = (pos + i) / 256, off = (pos + i) % 256;
      uint32_t L = (uint32_t)((p + N - W) % N);                 // logical index
      uint8_t rec[16];
      uint32_t key = T0 + 5 * L; int32_t serial = S0 + (int32_t)L; double hours = key / 5.0;
      memcpy(rec, &key, 4); memcpy(rec + 4, &serial, 4); memcpy(rec + 8, &hours, 8);
      out[i] = off < 16 ? rec[off] : 0;
    }
  };
  IotaLog log(256, 5, 365, 0);
  TEST_ASSERT_EQUAL_INT(0, log.begin(PATH));
  TEST_ASSERT_EQUAL_INT(0, ESP.restarts);
  TEST_ASSERT_EQUAL_UINT32(T0, log.firstKey());
  TEST_ASSERT_EQUAL_UINT32(T0 + 5 * (N - 1), log.lastKey());
  std::vector<ModelRec> ends = {{T0, S0}, {T0 + 5 * (N - 1), S0 + (int32_t)N - 1}};
  for(uint32_t L : {0u, 1u, 999u, 1000u, 1001u, 8388608u, 9000000u, N / 2, N - 2, N - 1}){
    IotaLogRecord r; r.UNIXtime = T0 + 5 * L;
    TEST_ASSERT_EQUAL_INT(0, log.readKey(&r));
    char msg[64]; snprintf(msg, sizeof msg, "logical record %u", L);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(S0 + (int32_t)L, r.serial, msg);
    TEST_ASSERT_EQUAL_DOUBLE_MESSAGE((T0 + 5.0 * L) / 5.0, r.logHours, msg);
  }
}

static void test_setDays_clamps(void){
  IotaLog a(256, 5, 365, 0);   TEST_ASSERT_EQUAL_UINT32(365, a.setDays(365));
  IotaLog b(256, 5, 1000, 0);  TEST_ASSERT_EQUAL_UINT32(970, b.setDays(1000));   // 4 GB limit (was 29 days)
  IotaLog c(256, 5, 0, 0);     TEST_ASSERT_EQUAL_UINT32(0, c.setDays(0));        // 1 hour minimum, reported as 0 days
}

// B58: reopening a log (end() then begin()) must not leak the previous path. Counted with ASan's
// allocator statistics: the live heap is the same after 100 reopen cycles as after the first.
extern "C" size_t __sanitizer_get_current_allocated_bytes();   // ASan runtime
static void test_reopen_does_not_leak_path(void){
  IotaLog log(256, 5, 365, 24);
  log.begin("/iotawatt/reopen.log"); log.end();
  log.begin("/iotawatt/reopen.log"); log.end();                 // warm up (SD stand-in, String buffers)
  size_t before = __sanitizer_get_current_allocated_bytes();
  for(int i = 0; i < 100; i++){ log.begin("/iotawatt/reopen.log"); log.end(); }
  size_t after = __sanitizer_get_current_allocated_bytes();
  TEST_ASSERT_TRUE_MESSAGE(after <= before + 64, "heap grew over 100 reopen cycles");
}

int main(int, char**){
  UNITY_BEGIN();
  RUN_TEST(test_new_log_is_empty);
  RUN_TEST(test_sequential_write_and_keyed_reads);
  RUN_TEST(test_holes_return_previous_record);
  RUN_TEST(test_write_rejects_non_increasing_keys);
  RUN_TEST(test_wrap_keeps_bounds_and_reads_exact);
  RUN_TEST(test_reopen_reconstructs_state);
  RUN_TEST(test_write_cache_matches_direct_writes);
  RUN_TEST(test_damaged_file_is_detected);
  RUN_TEST(test_truncated_file_does_not_crash);
  RUN_TEST(test_fuzz_random_workload);
  RUN_TEST(test_huge_wrapped_log_over_2GB);
  RUN_TEST(test_setDays_clamps);
  RUN_TEST(test_reopen_does_not_leak_path);
  return UNITY_END();
}
