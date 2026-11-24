# The Arduino core force-links float scanf (-u _scanf_float). IotaWatt only sscanf()s integers
# (strtof is unaffected), so drop it to save flash.
Import("env")
f = env["LINKFLAGS"]
out = []
i = 0
while i < len(f):
    if f[i] == "-u" and i + 1 < len(f) and f[i + 1] == "_scanf_float":
        i += 2
        continue
    if f[i] == "-u_scanf_float":
        i += 1
        continue
    out.append(f[i]); i += 1
env.Replace(LINKFLAGS=out)
