# Dependency lock (supply chain): before a build, check that every package and library it uses, and the
# vendored web files, are exactly the ones recorded in Firmware/dependencies.lock. A mismatch fails the build.
#
# What is hashed: each package and library directory as a tree (relative paths and SHA-256 of the
# contents; PlatformIO's own .piopm notes, .git and caches left out), so a changed, added or removed file
# shows. The lock entry is the environment's own, or the one named by `custom_lock_env` (core3_local
# uses core3's).
#
# To record a new state after changing a version in platformio.ini, build once with
#   IOTAWATT_LOCK_UPDATE=1 pio run -e <env>      (and pio test -e native for the native entry)
# then review and commit the change to dependencies.lock with the platformio.ini change.
#
# CI installs PlatformIO itself from Firmware/requirements-ci.txt (exact versions with SHA-256, pip
# --require-hashes). To move to another PlatformIO version X: `pip download platformio==X -d <empty dir>`,
# then write one line per wheel, `name==version --hash=sha256:<sha256 of the wheel>` (keep a platform-
# independent wheel's hash beside a platform-specific one), check it with
# `pip download --require-hashes -r requirements-ci.txt -d <empty dir>`, and commit it with the lock.
#
# Hashing ~400 MB of toolchain on every build would be slow: a tree's hash is reused while none of its
# files changed size or time since it was last hashed (cache in .pio/, never committed).

import hashlib, json, os, sys

Import("env")

PROJECT = env.subst("$PROJECT_DIR")
LOCK = os.path.join(PROJECT, "dependencies.lock")
CACHE = os.path.join(env.subst("$PROJECT_WORKSPACE_DIR"), "lock-cache.json")
SKIP_DIRS = {".git", "__pycache__", ".pio", ".github"}
SKIP_FILES = {".piopm"}
VENDORED = ["bootstrap.min.css", "bootstrap.min.js", "uplot.min.css", "uplot.min.js", "prism.min.js"]


def walk(path):
    for root, dirs, files in os.walk(path):
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS)
        for f in sorted(files):
            if f in SKIP_FILES or f.endswith(".pyc"):
                continue
            full = os.path.join(root, f)
            yield os.path.relpath(full, path).replace(os.sep, "/"), full


def fingerprint(path):
    h = hashlib.sha256()
    for rel, full in walk(path):
        st = os.lstat(full)
        h.update(("%s\0%d\0%d\n" % (rel, st.st_size, st.st_mtime_ns)).encode())
    return h.hexdigest()


def tree_hash(path):
    h = hashlib.sha256()
    for rel, full in walk(path):
        h.update(rel.encode() + b"\0")
        if os.path.islink(full):
            h.update(b"link:" + os.readlink(full).encode())
        else:
            f = hashlib.sha256()
            with open(full, "rb") as fh:
                for block in iter(lambda: fh.read(1 << 20), b""):
                    f.update(block)
            h.update(f.digest())
        h.update(b"\n")
    return h.hexdigest()


def file_hash(path):
    with open(path, "rb") as fh:
        return hashlib.sha256(fh.read()).hexdigest()


def load(path, default):
    try:
        with open(path) as fh:
            return json.load(fh)
    except (OSError, ValueError):
        return default


cache = load(CACHE, {})


def cached_tree_hash(path):
    fp = fingerprint(path)
    hit = cache.get(path)
    if hit and hit.get("fingerprint") == fp:
        return hit["sha256"]
    digest = tree_hash(path)
    cache[path] = {"fingerprint": fp, "sha256": digest}
    return digest


def version_of(path):
    for name in ("package.json", "library.json"):
        data = load(os.path.join(path, name), None)
        if isinstance(data, dict) and data.get("version"):
            return str(data["version"])
    try:
        with open(os.path.join(path, "library.properties")) as fh:
            for line in fh:
                if line.startswith("version="):
                    return line.split("=", 1)[1].strip()
    except OSError:
        pass
    return ""


def current_state():
    state = {"packages": {}, "libraries": {}}
    platform = env.PioPlatform()
    for pkg in platform.dump_used_packages():
        name = pkg["name"]
        path = platform.get_package_dir(name)
        if path and os.path.isdir(path):
            state["packages"][name] = {"version": version_of(path) or str(pkg.get("version", "")),
                                       "sha256": cached_tree_hash(path)}
    libdeps = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"))
    if os.path.isdir(libdeps):
        for name in sorted(os.listdir(libdeps)):
            path = os.path.join(libdeps, name)
            if os.path.isdir(path) and not name.startswith("."):
                state["libraries"][name] = {"version": version_of(path), "sha256": cached_tree_hash(path)}
    return state


def vendored_state():
    sd = os.path.normpath(os.path.join(PROJECT, "..", "SD"))
    return {name: file_hash(os.path.join(sd, name)) for name in VENDORED if os.path.isfile(os.path.join(sd, name))}


def differences(want, have):
    out = []
    for kind, one in (("packages", "package"), ("libraries", "library")):
        w, h = want.get(kind, {}), have.get(kind, {})
        for name in sorted(set(w) | set(h)):
            if name not in h:
                out.append("%s %s: in the lock, not installed" % (one, name))
            elif name not in w:
                out.append("%s %s %s: installed, not in the lock" % (one, name, h[name]["version"]))
            elif w[name] != h[name]:
                out.append("%s %s: lock %s %s..., installed %s %s..." % (one, name, w[name]["version"],
                           w[name]["sha256"][:12], h[name]["version"], h[name]["sha256"][:12]))
    return out


key = env.GetProjectOption("custom_lock_env", env.subst("$PIOENV"))
lock = load(LOCK, {"envs": {}, "vendored": {}})
have = current_state()
vend = vendored_state()

if os.environ.get("IOTAWATT_LOCK_UPDATE") == "1":
    lock.setdefault("envs", {})[key] = have
    lock["vendored"] = vend
    with open(LOCK, "w") as fh:
        json.dump(lock, fh, indent=2, sort_keys=True)
        fh.write("\n")
    print("lock_check: recorded %s in dependencies.lock (%d packages, %d libraries)"
          % (key, len(have["packages"]), len(have["libraries"])))
else:
    problems = []
    if key not in lock.get("envs", {}):
        problems.append("no entry for %s: record it with IOTAWATT_LOCK_UPDATE=1" % key)
    else:
        problems += differences(lock["envs"][key], have)
    for name, digest in sorted(lock.get("vendored", {}).items()):
        if vend.get(name) != digest:
            problems.append("vendored SD/%s: not the recorded file" % name)
    if problems:
        sys.stderr.write("lock_check: the dependencies don't match dependencies.lock (%s):\n  %s\n"
                         "If a version was changed on purpose, record it (see scripts/lock_check.py).\n"
                         % (key, "\n  ".join(problems)))
        env.Exit(1)
    print("lock_check: %s matches dependencies.lock (%d packages, %d libraries, %d vendored files)"
          % (key, len(have["packages"]), len(have["libraries"]), len(vend)))

os.makedirs(os.path.dirname(CACHE), exist_ok=True)
with open(CACHE, "w") as fh:
    json.dump(cache, fh)
