# Fail the build when the firmware gets close to the device's limits, not only when it no longer fits.
#
# PlatformIO stops the build only at 100%. Static RAM (.data, .rodata, .bss) is the one that bites first
# on the ESP8266: every byte of it comes out of the heap the web server, uploaders and log buffers run
# on (about 26 KB free on a running IoTaWatt). Defaults: program flash 85%, static RAM 55%. Set per
# environment in platformio.ini:
#
#   custom_max_flash_percent = 85
#   custom_max_ram_percent = 55
#
# Sizes are measured as PlatformIO's own size report does (its SIZEPROGREGEXP/SIZEDATAREGEXP), so the
# percentages match the "RAM:" and "Flash:" lines of the build output.
Import("env")
import re
import subprocess

DEFAULT_FLASH_PERCENT = 85
DEFAULT_RAM_PERCENT = 55


def _limit(name, default):
    value = env.GetProjectOption(name, default)
    try:
        return float(value)
    except (TypeError, ValueError):
        print("size_check: %s = %r is not a number" % (name, value))
        env.Exit(1)


def _sum(regexp, lines):
    total = 0
    for line in lines:
        m = re.match(regexp, line)
        if m:
            total += int(m.group(1))
    return total


def size_check(target, source, env):
    elf = env.subst("$BUILD_DIR/${PROGNAME}.elf")
    out = subprocess.run([env.subst("$SIZETOOL"), "-A", "-d", elf], env=env["ENV"],
                         capture_output=True, text=True, check=True).stdout.splitlines()
    prog = _sum(env["SIZEPROGREGEXP"], out)
    data = _sum(env["SIZEDATAREGEXP"], out)
    board = env.BoardConfig()
    max_prog = int(board.get("upload.maximum_size", 0))
    max_data = int(board.get("upload.maximum_ram_size", 0))
    if not max_prog or not max_data:
        print("size_check: no size limits for this board; not checked")
        return 0

    flash_limit = _limit("custom_max_flash_percent", DEFAULT_FLASH_PERCENT)
    ram_limit = _limit("custom_max_ram_percent", DEFAULT_RAM_PERCENT)
    failed = False
    for what, used, maximum, limit in (("Flash", prog, max_prog, flash_limit),
                                       ("RAM", data, max_data, ram_limit)):
        percent = 100.0 * used / maximum
        allowed = int(maximum * limit / 100)
        if used > allowed:
            print("size_check: %s %d bytes = %.1f%% of %d, over the %g%% limit by %d bytes"
                  % (what, used, percent, maximum, limit, used - allowed))
            failed = True
        else:
            print("size_check: %s %.1f%% (limit %g%%, %d bytes to spare)" % (what, percent, limit, allowed - used))
    if failed:
        print("size_check: build refused. Make room, or raise the limit in platformio.ini knowingly.")
        return 1
    return 0


# After PlatformIO's own size check: on the ESP8266 that step is what narrows upload.maximum_size from
# the flash chip to the program area of the linker script (1,044,464 bytes with eagle.flash.4m1m.ld).
env.AddPostAction("checkprogsize", env.VerboseAction(size_check, "Checking size limits (scripts/size_check.py)"))
