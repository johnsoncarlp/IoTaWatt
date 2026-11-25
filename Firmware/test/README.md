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
| `test_jsonsplit` | `JsonSummary`/`JsonDetail` (config splitter): summary + condensed parts at depth 1 and 2 vs a reference, brackets and escapes inside strings; unbalanced, unterminated, empty, deep nesting, random fuzz |
| `test_config` | `setConfig()` end to end with the real tables.txt: device, timezone, DST, inputs (turns/burden, double, reverse, signed), outputs, integrations, uploaders, PVOutput, simsolar, burden file, phase tables + lookup, channel change, comments, unquoted-value recovery, out-of-range values, wrong types, long table arrays, reload leak check, mutation fuzz |
| `test_query` | `/query` on a real IotaLog with synthetic data: hourly/daily/raw/auto groups, every unit and decimals, JSON/CSV, ISO/relative/unix times, limit and the 60 s time budget (all 3 outcomes), missing=null/zero/skip, show=series, every validation error, zero-length groups, argument fuzz |
| `test_release` | `unpackUpdate()` on releases built and signed here (test Ed25519 key, official format): valid release unpacks every file; wrong key, tampered body or signature, bad magic or version, missing file, no firmware are rejected |
| `test_uploaders` | influxDB v1/v2 and Emoncms through their real state machines with a recording fake HTTP client: query and write bodies, URLs, auth headers, line protocol and Emoncms bulk frames vs synthetic data, Emoncms AES-128-CBC/HMAC vs Python, failures and retries, config validation, status, no WiFi, lifecycle leak check, fuzzed server replies |
| `test_datalog` | `dataLog()` through a simulated scheduler: the clock behind the log after a restart and while running (bounded polling, watchdog fed, logged, resumes after the last entry), large forward step |
| `test_pvoutput` | PVoutput uploader through its real state machine: end before start / while running / while the server is unreachable (instance and global released), removed and re-added before stopping, bad configs |
| `test_webserver` | Web handlers against a fake web server that records responses: `/status` sections (stats, wifi, inputs, outputs, uploaders, datalogs, passwords, device), `/vcal` and `/command` incl. out-of-range channels, password set/change/clear and bad JSON, feed list, graph save/list/delete incl. bad graph files, SD directory listing, `/query` streaming and its time-limit abort |
