# Host unit tests

Run on a PC, no device needed:

    cd Firmware
    pio test -e native

Each `test_*` folder is one test program (Unity). Device code is compiled directly from `../IotaWatt`
(`test_build_src = no`); `test/shim/` stands in for the Arduino headers the code under test needs.

| Test | Covers |
|---|---|
| `test_jsonutil` | `jsonUtil.h`: ArduinoJson 5 compatible conversions, checked against values generated from ArduinoJson 5.13.4 |
| `test_splitstr` | `splitstr` parsing, trimming and edge cases (run under AddressSanitizer) |
| `test_ntp` | `ntpReplyPlausible()`: NTP reply validation across 2028, the 2036 NTP rollover, up to 2106 |
