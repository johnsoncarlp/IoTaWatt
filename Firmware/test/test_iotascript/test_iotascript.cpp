// IotaScript: the output/integration script engine (left-to-right, no precedence; @n input, #k constant,
// !name integration, ~0 simulated solar; + - * / < (min) > (max) | (abs) ( ) ).
#include <unity.h>
#include "host_iotawatt.h"
#include "host_script_deps.h"
#include "IotaScript.cpp"

static IotaInputChannel* chans[3];
static IotaLogRecord oldRec, newRec;

void setUp(void){
  hostLog.clear();
  for(int i = 0; i < 3; i++){ chans[i] = new IotaInputChannel(i); chans[i]->_vmult = 1; chans[i]->_vchannel = 0; }
  chans[2]->_vmult = 2;                       // a "double" CT
  inputChannel = chans; maxInputs = 3;
  integrations = new ScriptSet();
  oldRec = IotaLogRecord(); newRec = IotaLogRecord();
  oldRec.UNIXtime = 1795560000; newRec.UNIXtime = oldRec.UNIXtime + 7200;
  oldRec.logHours = 100; newRec.logHours = 102;                       // 2 hours apart
  oldRec.accum1[0] = 5000;  newRec.accum1[0] = 5000 + 240;            // volt-hours -> 120 V
  oldRec.accum2[0] = 7000;  newRec.accum2[0] = 7000 + 120;            // Hz-hours   -> 60 Hz
  oldRec.accum1[1] = 1e6;   newRec.accum1[1] = 1e6 + 2000;            // Wh  -> 1000 W
  oldRec.accum2[1] = 2e6;   newRec.accum2[1] = 2e6 + 2500;            // VAh -> 1250 VA
  oldRec.accum1[2] = 0;     newRec.accum1[2] = 300;
  oldRec.accum2[2] = 0;     newRec.accum2[2] = 500;
}
void tearDown(void){
  delete integrations; integrations = nullptr;
  for(auto& c : chans) delete c;
}

static double runAs(const char* script, const char* unit){
  Script s("t", unit, script);
  return s.run(&oldRec, &newRec);
}
#define NEAR(expect, got) TEST_ASSERT_DOUBLE_WITHIN(1e-9 * fabs(expect) + 1e-12, expect, got)

static void test_units_for_one_input(void){
  NEAR(1000.0, runAs("@1", "Watts"));
  NEAR(120.0,  runAs("@1", "Volts"));                 // via the input's voltage reference
  NEAR(1250.0 / 120.0, runAs("@1", "Amps"));
  NEAR(1250.0, runAs("@1", "VA"));                    // sqrt(VAR^2 + W^2)
  NEAR(2500.0, runAs("@1", "VAh"));
  NEAR(60.0,   runAs("@1", "Hz"));
  NEAR(2000.0, runAs("@1", "Wh"));
  NEAR(2.0,    runAs("@1", "kWh"));
  NEAR(0.8,    runAs("@1", "PF"));
  NEAR(750.0,  runAs("@1", "VAR"));
  NEAR(1500.0, runAs("@1", "VARh"));
  NEAR(240.0,  runAs("@2", "Volts"));                 // _vmult 2 doubles the reference voltage
}

static void test_without_old_record_uses_new_totals(void){
  Script s("t", "Watts", "@1");
  NEAR(newRec.accum1[1], s.run(nullptr, &newRec));    // elapsed hours = 1
}

static void test_left_to_right_no_precedence(void){
  NEAR(20.0, runAs("#2+#3*#4", "Watts"));             // (2+3)*4
  NEAR(14.0, runAs("#2+(#3*#4)", "Watts"));
  NEAR(3.0,  runAs("#10-#4-#3", "Watts"));
  NEAR(2.0,  runAs("#8/#2/#2", "Watts"));
  NEAR(3.0,  runAs("#5<#3", "Watts"));                // min
  NEAR(5.0,  runAs("#5>#3", "Watts"));                // max
  NEAR(-5.0, runAs("#0-(#0-#5)|", "Watts"));          // | = abs of the operand
  NEAR(1.5,  runAs("#1.5", "Watts"));
  NEAR(1150.0, runAs("@1+@2", "Watts"));             // 1000 W + 300 Wh / 2 h
  NEAR(850.0,  runAs("@1-@2", "Watts"));
  NEAR(((2.0 + 3.0) * (4.0 - 1.0)) / 5.0, runAs("((#2+#3)*(#4-#1))/#5", "Watts"));
}

static void test_divide_by_zero_is_zero(void){
  NEAR(0.0, runAs("#5/#0", "Watts"));
  NEAR(0.0, runAs("@1/(@2-@2)", "Watts"));
}

static void test_noisy_reactive_power_is_zero_not_nan(void){
  newRec.accum2[1] = oldRec.accum2[1] + 1999.9;      // VA a hair below W (noise near unity PF)
  double var = runAs("@1", "VAR");
  TEST_ASSERT_FALSE(std::isnan(var));
  NEAR(0.0, var);
  NEAR(1000.0 / 999.95, runAs("@1", "PF"));          // PF = W / measured VA: noise can take it just above 1
}

static void test_integrations(void){
  integrator solar; solar.value = 42;
  // ScriptSet has no add(); build one from JSON, then attach the fake integrator.
  JsonDocument d; JsonArray a = d.to<JsonArray>();
  JsonObject o = a.add<JsonObject>(); o["name"] = "solar"; o["units"] = "Wh"; o["script"] = "@1";
  delete integrations; integrations = new ScriptSet(a);
  integrations->first()->setParm(&solar);
  NEAR(42.0, runAs("!solar", "Wh"));
  Script plus("t", "Wh", "!+solar");
  plus.run(&oldRec, &newRec);
  TEST_ASSERT_EQUAL_CHAR('+', solar.lastMethod);
  NEAR(0.0, runAs("!nope", "Wh"));                   // undefined integration: logged, treated as 0
  bool logged = false; for(auto& l : hostLog) logged |= l.find("integration nope not defined") != std::string::npos;
  TEST_ASSERT_TRUE(logged);
}

static void test_precision_per_unit(void){
  struct { const char* unit; int decimals; } p[] = {
    {"Watts", 2}, {"Volts", 2}, {"Amps", 3}, {"VA", 2}, {"VAh", 4}, {"Hz", 2},
    {"Wh", 4}, {"kWh", 7}, {"PF", 3}, {"VAR", 2}, {"VARh", 4}};
  for(auto& x : p){ Script s("t", x.unit, "@1"); TEST_ASSERT_EQUAL_INT_MESSAGE(x.decimals, s.precision(), x.unit); }
}

static void test_units_name_is_case_insensitive(void){
  Script s("t", "kwh", "@1");
  TEST_ASSERT_EQUAL_STRING("kWh", s.getUnits());
  NEAR(2.0, s.run(&oldRec, &newRec));
}

static void test_scriptset_from_json_and_sort(void){
  JsonDocument d;
  TEST_ASSERT_FALSE(deserializeJson(d, "[{\"name\":\"b\",\"units\":\"Watts\",\"script\":\"@1\"},"
                                       "{\"name\":123,\"units\":\"Watts\",\"script\":\"@2\"},"
                                       "{\"name\":\"a\",\"units\":\"Volts\",\"script\":\"@1\"}]"));
  ScriptSet set(d.as<JsonArray>());
  TEST_ASSERT_EQUAL_INT(3, set.count());
  TEST_ASSERT_NOT_NULL(set.script("123"));            // numeric name read as text (jStr)
  set.sort([](Script* x, Script* y){ return strcmp(x->name(), y->name()); });
  TEST_ASSERT_EQUAL_STRING("123", set.first()->name());
  TEST_ASSERT_EQUAL_STRING("a", set.first()->next()->name());
  TEST_ASSERT_EQUAL_STRING("b", set.first()->next()->next()->name());
  ScriptSet empty; empty.sort([](Script*, Script*){ return 0; });   // B15: no crash
}

// ---- bad input ----

static void test_invalid_scripts_evaluate_to_zero(void){
  const char* bad[] = {"", "#1x#2", "@", "@-1", "@3", "@15", "@32", "@999999999999", "#", "((#1", "#1)", ")(", "|||", "<>"};
  for(const char* b : bad){
    double v = runAs(b, "Watts");
    TEST_ASSERT_FALSE_MESSAGE(std::isnan(v), b);
  }
  NEAR(0.0, runAs("@15", "Watts"));                   // B26: out-of-range input rejected
  NEAR(0.0, runAs("@32", "Watts"));
}

static void test_many_constants(void){
  // B37: the constant index is a 5-bit token field. 31 constants work; more are rejected (0), not garbage.
  std::string s = "#1";
  for(int i = 1; i < 31; i++) s += "+#1";
  NEAR(31.0, runAs(s.c_str(), "Watts"));
  for(int i = 31; i < 40; i++) s += "+#1";
  NEAR(0.0, runAs(s.c_str(), "Watts"));
}

static void test_long_integration_name(void){
  // The integration name is copied into a fixed buffer; config can hold any length.
  std::string s = "!" + std::string(64, 'x');
  double v = runAs(s.c_str(), "Wh");
  TEST_ASSERT_FALSE(std::isnan(v));
}

static void test_fuzz_random_scripts(void){
  uint32_t seed = 99;
  auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
  const char alphabet[] = "@#!~0123456789.+-*/<>|()xs";
  for(int iter = 0; iter < 20000; iter++){
    char s[48]; int len = rnd() % 45;
    for(int i = 0; i < len; i++) s[i] = alphabet[rnd() % (sizeof(alphabet) - 1)];
    s[len] = 0;
    double v = runAs(s, (rnd() % 2) ? "Watts" : "PF");
    TEST_ASSERT_FALSE_MESSAGE(std::isnan(v), s);
  }
}

int main(int, char**){
  UNITY_BEGIN();
  RUN_TEST(test_units_for_one_input);
  RUN_TEST(test_without_old_record_uses_new_totals);
  RUN_TEST(test_left_to_right_no_precedence);
  RUN_TEST(test_divide_by_zero_is_zero);
  RUN_TEST(test_noisy_reactive_power_is_zero_not_nan);
  RUN_TEST(test_integrations);
  RUN_TEST(test_precision_per_unit);
  RUN_TEST(test_units_name_is_case_insensitive);
  RUN_TEST(test_scriptset_from_json_and_sort);
  RUN_TEST(test_invalid_scripts_evaluate_to_zero);
  RUN_TEST(test_many_constants);
  RUN_TEST(test_long_integration_name);
  RUN_TEST(test_fuzz_random_scripts);
  return UNITY_END();
}
