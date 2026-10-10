#!/usr/bin/env python3
"""Build the widescreen simulator for the browser (WebAssembly via Emscripten).

The source list and flags come from PlatformIO's compile database for
env:display-sim-wide, so this stays in step with platformio.ini. Each file is
recompiled with emcc/em++ and linked into a static site.

    source ~/emsdk/emsdk_env.sh
    python3 sim-web/build.py            # -> .pio/build/display-web/site/
    python3 sim-web/build.py --serve    # build, then serve on http://localhost:8000

Needs sdl2-config on PATH (PlatformIO evaluates it while writing the database).
"""
import argparse
import concurrent.futures
import functools
import http.server
import json
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENV = "display-sim-wide"
OUT = ROOT / ".pio" / "build" / "display-web"
SITE = OUT / "site"
WEB_DIR = Path(__file__).resolve().parent

# The .incbin'd web UI blob is replaced by web_ui_blob_stub.c.
SKIP = {"sim/web/web_ui_blob_sim.S"}
EXTRA_SOURCES = ["sim-web/web_ui_blob_stub.c"]

COMPILE_FLAGS = ["-O2", "-sUSE_SDL=2", "-DGAGGIMATE_SIM_WEB", "-Wno-unused-command-line-argument"]
LINK_FLAGS = [
    "-O2",
    "-sUSE_SDL=2",
    "-sASYNCIFY",  # delay() in the firmware's loop yields to the browser
    "-sASYNCIFY_STACK_SIZE=65536",
    "-sSTACK_SIZE=1MB",
    "-sALLOW_MEMORY_GROWTH=1",
    "-sINITIAL_MEMORY=64MB",
    "-sENVIRONMENT=web",
    "-lidbfs.js",
    f"--shell-file={WEB_DIR / 'shell.html'}",
]


def find_pio():
    for candidate in (os.environ.get("PIO"), Path.home() / ".platformio/penv/bin/pio", shutil.which("pio")):
        if candidate and Path(candidate).exists():
            return str(candidate)
    sys.exit("PlatformIO not found; set PIO=/path/to/pio")


def compile_database():
    subprocess.run([find_pio(), "run", "-e", ENV, "-t", "compiledb"], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    OUT.mkdir(parents=True, exist_ok=True)
    db_path = OUT / "compile_commands.json"
    shutil.move(ROOT / "compile_commands.json", db_path)
    return json.loads(db_path.read_text())


def translate(entry):
    """Host compile command -> emcc command writing into OUT."""
    args = entry.get("arguments") or shlex.split(entry["command"])
    source = entry["file"]
    keep = []
    skip_next = False
    for arg in args[1:]:
        if skip_next:
            skip_next = False
            continue
        if arg in ("-o", "-c"):
            skip_next = arg == "-o"
            continue
        if arg == source or arg.startswith("-I") and "SDL2" in arg or arg == "-D_REENTRANT":
            continue
        keep.append(arg)
    compiler = "em++" if source.endswith(".cpp") else "emcc"
    obj = OUT / "obj" / (source.lstrip("/").replace("..", "_") + ".o")
    return source, obj, [compiler, *keep, *COMPILE_FLAGS, "-MMD", "-c", source, "-o", str(obj)]


def is_stale(source, obj):
    if not obj.exists():
        return True
    built = obj.stat().st_mtime
    dep = obj.with_suffix(".d")
    if not dep.exists():
        return True
    deps = dep.read_text().replace("\\\n", " ").split(":", 1)[-1].split()
    return any(not (ROOT / d).exists() or (ROOT / d).stat().st_mtime > built for d in [source, *deps])


def compile_one(job):
    source, obj, cmd = job
    obj.parent.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    return source, result


def build():
    if not shutil.which("emcc"):
        sys.exit("emcc not found; run: source ~/emsdk/emsdk_env.sh")
    db = compile_database()
    template = next(e for e in db if e["file"].endswith(".c"))
    jobs, seen = [], set()
    for entry in db:
        if entry["file"] in SKIP or entry["file"] in seen:
            continue
        seen.add(entry["file"])
        jobs.append(translate(entry))
    for extra in EXTRA_SOURCES:
        jobs.append(translate({**template, "file": extra, "command": None,
                               "arguments": (template.get("arguments") or shlex.split(template["command"]))[:1] + [extra]}))

    stale = [job for job in jobs if is_stale(job[0], job[1])]
    print(f"Compiling {len(stale)} of {len(jobs)} files")
    failed = False
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as pool:
        for source, result in pool.map(compile_one, stale):
            if result.returncode != 0:
                failed = True
                print(f"--- {source}\n{result.stderr}", file=sys.stderr)
    if failed:
        sys.exit("Compile failed")

    SITE.mkdir(parents=True, exist_ok=True)
    print("Linking")
    subprocess.run(["em++", *[str(job[1]) for job in jobs], *LINK_FLAGS, "-o", str(SITE / "index.html")], cwd=ROOT,
                   check=True)
    print(f"Built {SITE / 'index.html'}")


def serve(port):
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(SITE))
    print(f"Serving http://localhost:{port}/")
    http.server.ThreadingHTTPServer(("127.0.0.1", port), handler).serve_forever()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--serve", action="store_true", help="serve the site after building")
    parser.add_argument("--port", type=int, default=8000)
    options = parser.parse_args()
    build()
    if options.serve:
        serve(options.port)
