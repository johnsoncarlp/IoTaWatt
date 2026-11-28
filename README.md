# IoTaWatt Reimagined

Firmware for the [IoTaWatt](https://iotawatt.com) energy monitor, version 3.0.0. It is a fork of the
original IoTaWatt firmware by Bob Lemaire ([boblemaire/IoTaWatt](https://github.com/boblemaire/IoTaWatt)).
The fork goes well beyond the original. It modernizes the build, fixes a long list of bugs, adds a test
suite, redesigns the web interface and adds new features. The full list is under
[What's different](#whats-different).

> **Not an official IoTaWatt release.** IoTaWatt Reimagined is not made, endorsed or supported by
> IoTaWatt, Inc. or Bob Lemaire. Please don't ask them for help with it. Use this repository's
> issues instead. "IoTaWatt" is used only to say which hardware this firmware runs on.

## Credit

IoTaWatt is Bob Lemaire's work: the hardware, the measurement and calibration code, the data logs, the
uploaders and the original web interface. This fork builds on years of his work, and on
[IoTaWatt, Inc.](https://stuff.iotawatt.com), which makes and certifies the hardware. If you don't
have an IoTaWatt yet, buy one there. The original project's description is kept in
[UPSTREAM-README.rst](UPSTREAM-README.rst). Its user documentation ([Docs/](Docs/), published at
[iotawatt.readthedocs.io](https://iotawatt.readthedocs.io)) still describes the original firmware.
Most of it applies here too.

Fixes for bugs that also affect the original firmware are offered back upstream as small, separate
pull requests.

The firmware is licensed under the GNU General Public License v3, as the original ([LICENSE](LICENSE)). The libraries
built into it keep their own licences: see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## What's different

Compared with the original firmware's last state (02_08_04, master of March 2026):

| Area | IoTaWatt Reimagined |
|---|---|
| Build | PlatformIO with the current ESP8266 Arduino core (3.1.2) and libraries (ArduinoJson 7). The original core-2.4 build still compiles. The build fails if the firmware gets close to the device's flash or RAM limits. CI on every push. |
| Bugs | More than 60 bugs in the original firmware fixed. Among them: time sync stopping for good on 1 Jan 2028, data log errors for logs over 2 GB, memory leaks and crashes in the uploaders, queries that could run until the watchdog restarted the device, and crashes or boot loops from unusual configurations. Each one is recorded with the upstream commit that introduced it. |
| Tests | A host test suite (17 programs, about 160 test cases) runs the firmware's own code on a PC under AddressSanitizer and UndefinedBehaviorSanitizer. Covered: the data log, scripts, DST rules, configuration, queries, release files, uploaders, the web server and more. |
| Web interface | Built into the firmware, so it no longer depends on files on the SD card (an SD copy can still override it), and it loads nothing from the internet: Graph+ and the file editor work offline. One menu bar on every page. A refreshed look (a trimmed Bootstrap 5) that adapts to phones, and a dark mode that follows the system. Graph+ is the single graph page (the original Graph page is retired). The status page has a summary line. Pages open in the same tab (only the message log gets its own), and leaving a page with unsaved changes asks first. |
| Backup and restore | Tools → Backup and restore saves the configuration to your computer and restores it: the file is checked, and what it holds (device name, inputs, outputs) is shown for you to confirm before it is written. |
| Files | A rebuilt file manager: folders with sizes, upload into the folder you're in under the file's own name, new files and folders, download and delete. The editor (built in, works offline) has line numbers, colours for JSON, HTML, JavaScript and CSS, a clear Save and Discard changes, a JSON check before `config.txt` is saved, and warns before leaving unsaved text. For heavier editing, download a file, edit it in your own editor and upload it back. Large text files (the message log) open read-only showing their last 10 KB. |
| Time | Time servers can be configured, or taken from DHCP. Sync interval setting. If a time server never answers, the next source is tried. The time zone is picked by name from a list (with its daylight saving rules), UTC by default. |
| Wi-Fi | Moves to a clearly stronger access point of the same network ("roaming"). Tools → Wi-Fi changes the network of a working IoTaWatt, with a scan of the networks in range, without going back to setup mode; if the new network doesn't connect within a minute, it returns to the old one. |
| Web server | Refuses requests with "503, retry" when memory is low or a single caller polls too fast, instead of running out of memory. |
| Updates | Never replaces itself with an official IoTaWatt release (auto-update is off). Firmware updates come from you: over USB for now, and from the web interface once its upload page is published here. |
| Code | Dead and duplicated code removed. Long functions split into named steps. |

Every change is tracked with its reason and origin, and every commit builds and passes the tests on
its own. `git log` explains each change.

## Status

IoTaWatt Reimagined runs on IoTaWatt 5.0 hardware. It has been tested on one device so far. More
reports from other devices and setups are very welcome (see [Contributing](#contributing)).

## Installing

Your configuration and data logs live on the SD card and are kept. Before you change firmware:

1. **Back up the SD card** (copy everything on it), or at least `config.txt` and the `iotawatt/` folder.
2. **Back up the current firmware** over USB: `esptool.py --port /dev/ttyUSB0 read_flash 0 0x400000 iotawatt-backup.bin`.
3. Note the firmware version shown on the Status page.

Flash the firmware image over USB:

    esptool.py --port /dev/ttyUSB0 write_flash 0x0 iotawatt-reimagined-3.0.0.bin

To go back to the original firmware, write your backup (or an official image) the same way.

To build it yourself, see [Building](#building).

## Building

    cd Firmware
    pio run -e core3            # the firmware: .pio/build/core3/firmware.bin
    pio test -e native          # the host tests

`Firmware/test/README.md` describes the tests.

## Integrations

Home Assistant, Telegraf, Node-RED and scripts read the device through its data plane: `/status`,
`/query` and `/feed/*`, documented in [Docs/api.rst](Docs/api.rst) (query parameters in
[Docs/query.rst](Docs/query.rst)). Give them a **user** login: it reads measurements and status, never
the configuration, passwords or files. Those are the control plane: the web interface, with the admin
login. The data plane stays compatible: fields are added, never removed.

## Contributing

Contributions from the community are expected and welcome. Use the **issues** for bugs, questions about
this firmware and ideas. Send changes as **pull requests**. A good pull request:

- does one thing, and explains why in the commit message;
- includes a test that fails without the change, for a bug fix;
- builds and passes `pio test -e native` at every commit.

For a security problem, don't open a public issue: use GitHub's private vulnerability reporting on
this repository (Security, "Report a vulnerability").
