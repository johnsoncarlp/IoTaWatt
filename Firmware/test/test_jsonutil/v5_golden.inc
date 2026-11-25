// Generated from ArduinoJson 5.13.4 (embedded mode, as on the ESP8266): as<int>, as<unsigned>, as<float>, as<bool>.
// Rows marked true are deliberate differences where v5 was buggy (documented in jsonUtil.h).
static const NumCase NUM_CASES[] = {
  {"15",                             15L,         15UL, 15.0f,             1, false},
  {"\"15\"",                         15L,         15UL, 15.0f,             1, false},
  {"\" 15\"",                         0L,          0UL, 0.0f,              0, false},
  {"\"15abc\"",                      15L,         15UL, 15.0f,             1, false},
  {"\"-3\"",                         -3L, 4294967293UL, -3.0f,             1, false},
  {"\"+7\"",                          7L,          7UL, 7.0f,              1, false},
  {"\"true\"",                        1L,          1UL, 1.0f,              1, false},
  {"\"false\"",                       0L,          0UL, 0.0f,              0, false},
  {"\"t\"",                           1L,          1UL, 1.0f,              1, false},
  {"\"\"",                            0L,          0UL, 0.0f,              0, false},
  {"\"abc\"",                         0L,          0UL, 0.0f,              0, false},
  {"true",                            1L,          1UL, 1.0f,              1, false},
  {"false",                           0L,          0UL, 0.0f,              0, false},
  {"2.5",                             2L,          2UL, 2.5f,            1, false},
  {"-2.5",                           -2L, 4294967294UL, -2.5f,           1, false},
  {"\"2.5\"",                         2L,          2UL, 2.5f,            1, false},
  {"\"-0.5\"",                        0L,          0UL, -0.5f,           0, false},
  {"1e3",                         1000L,       1000UL, 1000.0f,         1, true},   // v5: int 1 (read the text "1e3" as digits); v7 parses the number
  {"\"1e3\"",                         1L,          1UL, 1000.0f,           1, false},
  {"null",                           0L,          0UL, 0.0f,            0, true},   // v5: float NaN
  {"<missing>",                       0L,          0UL, 0.0f,              0, false},
  {"0",                               0L,          0UL, 0.0f,              0, false},
  {"-1",                             -1L, 4294967295UL, -1.0f,             1, false},
  {"4294967295",                     -1L, 4294967295UL, 4.2949673e+09f,  1, false},
  {"3000000000",            -1294967296L, 3000000000UL, 3e+09f,          1, false},
  {"[1]",                            0L,          0UL, 0.0f,            0, true},   // v5: garbage (pointer bits)
  {"{\"a\":1}",                     0L,          0UL, 0.0f,            0, true},   // v5: garbage (pointer bits)
  {"0.5",                             0L,          0UL, 0.5f,            0, false},
  {"\"1.25e2\"",                      1L,          1UL, 125.0f,            1, false},
  {"-300",                         -300L, 4294966996UL, -300.0f,           1, false},
  {"\"nan\"",                         0L,          0UL, NAN,             0, false},
  {"\"-inf\"",                        0L,          0UL, -INFINITY,       0, false},
  {"\"1.5e\"",                        1L,          1UL, 1.5f,            1, false},
  {"\".\"",                           0L,          0UL, 0.0f,              0, false},
  {"\"-.5\"",                         0L,          0UL, -0.5f,           0, false},
  {"\"0x10\"",                        0L,          0UL, 0.0f,              0, false},
  {"\"1e-2\"",                        1L,          1UL, 0.00999999978f,  1, false},
  {"\"+t\"",                          0L,          0UL, 1.0f,              0, false},
  {"\"12.5.3\"",                     12L,         12UL, 12.5f,           1, false},
};

// as<const char*>() (NULL -> "")
static const StrCase STR_CASES[] = {
  {"12345",       "12345"},
  {"-7",          "-7"},
  {"1.5",         "1.5"},
  {"true",        "true"},
  {"false",       "false"},
  {"null",        ""},
  {"[1]",         ""},
  {"{\"a\":1}",   ""},
  {"\"\"",        ""},
  {"\"abc\"",     "abc"},
  {"\"12\"",      "12"},
  {"0",           "0"},
};
