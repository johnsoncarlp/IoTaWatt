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
| `test_iotalog` | IotaLog against a model: keyed reads, holes, bounds, wrap, reopen/findWrap, write cache, damaged and truncated files, random workload, a synthesized 3.84 GB wrapped log (>2 GB math) |
| `test_iotascript` | Script engine: every unit, left-to-right evaluation, parentheses, min/max/abs, divide by zero, integrations, per-unit precision, JSON ScriptSet + sort; bad scripts, 31+ constants, long integration names, random-script fuzz |
| `test_timerules` | `UTC2Local`/`local2UTC`/`testRule` with the four web-UI DST presets vs the IANA tz database (13 zones, 21,736 samples, 2024-2045), round trip, leap-year February, bad-rule fuzz |
