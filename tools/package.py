#!/usr/bin/env python3
"""Packs a finished build into the release folder/archive for one platform.

  python3 tools/package.py --build build --platform minimaxLinuxX64 --out dist [--qt-bin /path/to/qt/bin]

Result: dist/<platform>/ and dist/<platform>.zip (Windows) or .tar.gz (Linux/macOS).
Layout (what the update server serves for this platform):
  update[.exe]  minimax[.exe] | minimax.app   configs/{client.toml,connect.ip,update_public.key}  + Qt runtime
CaptchaAPI.cfg is never copied; the script fails if it ends up in the package.
"""
import argparse, os, re, shutil, subprocess, sys, tarfile, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUNDLE_LIBS = re.compile(r"^(libQt[56]|libicu|libdouble-conversion|libpcre2-16|libb2|libmd4c|libxcb-(cursor|icccm|image|keysyms|render-util|shape|sync|xinerama|xkb|randr|util)|libxkbcommon)")


def run(*cmd, **kw):
    print("+", " ".join(map(str, cmd)))
    return subprocess.run(list(map(str, cmd)), check=True, **kw)


def ldd(path):
    out = subprocess.run(["ldd", str(path)], capture_output=True, text=True).stdout
    return [Path(m.group(1)) for m in re.finditer(r"=>\s+(/\S+)", out)]


def find_tool(name, qt_bin):
    for d in ([qt_bin] if qt_bin else []) + [None]:
        p = shutil.which(name, path=d) if d else shutil.which(name)
        if p:
            return p
    sys.exit(f"{name} not found (use --qt-bin)")


def prepare_configs(build: Path, dest: Path):
    (dest / "configs").mkdir(parents=True, exist_ok=True)
    shutil.copy(ROOT / "configs/client.toml", dest / "configs/client.toml")
    shutil.copy(ROOT / "configs/update_public.key", dest / "configs/update_public.key")
    ip = build / "client/configs/connect.ip"
    if not ip.exists():
        ip = next(build.rglob("connect.ip"), None)
    if ip:
        shutil.copy(ip, dest / "configs/connect.ip")
    else:
        (dest / "configs/connect.ip").write_text("# MINI max: server address (one line)\n127.0.0.1:8080\n")


def pack_linux(build: Path, dest: Path, qt_bin):
    for n in ("minimax", "update"):
        shutil.copy(build / ("client" if n == "minimax" else "updater") / n, dest / n)
    lib = dest / "lib"; lib.mkdir()
    plugins = dest / "plugins"; plugins.mkdir()
    qt_plugin_root = None
    if qt_bin:
        qt_plugin_root = Path(qt_bin).parent / "plugins"
    if not qt_plugin_root or not qt_plugin_root.exists():
        out = subprocess.run(["qmake6", "-query", "QT_INSTALL_PLUGINS"], capture_output=True, text=True).stdout.strip() or subprocess.run(["qmake", "-query", "QT_INSTALL_PLUGINS"], capture_output=True, text=True).stdout.strip()
        qt_plugin_root = Path(out)
    wanted = ["platforms/libqxcb.so", "imageformats", "iconengines", "xcbglintegrations", "platformthemes", "tls", "wayland-shell-integration"]
    copied = []
    for w in wanted:
        src = qt_plugin_root / w
        if src.is_file():
            (plugins / Path(w).parent).mkdir(parents=True, exist_ok=True); shutil.copy(src, plugins / w); copied.append(plugins / w)
        elif src.is_dir():
            shutil.copytree(src, plugins / w)
            copied += [p for p in (plugins / w).rglob("*.so")]
    targets = [dest / "minimax", dest / "update"] + copied
    seen = set()
    for t in targets:
        for dep in ldd(t):
            if BUNDLE_LIBS.match(dep.name) and dep.name not in seen:
                seen.add(dep.name); shutil.copy(dep.resolve(), lib / dep.name)
    # second pass: dependencies of the bundled libs themselves
    for l in list(lib.iterdir()):
        for dep in ldd(l):
            if BUNDLE_LIBS.match(dep.name) and not (lib / dep.name).exists():
                shutil.copy(dep.resolve(), lib / dep.name)
    patchelf = shutil.which("patchelf")
    if patchelf:
        for t in (dest / "minimax", dest / "update"):
            run(patchelf, "--set-rpath", "$ORIGIN/lib", t)
        for p in copied:
            rel = os.path.relpath(lib, p.parent)
            run(patchelf, "--set-rpath", f"$ORIGIN/{rel}", p)
        for l in lib.iterdir():
            run(patchelf, "--set-rpath", "$ORIGIN", l)
    else:
        print("WARNING: patchelf missing - the package will rely on system Qt")
    (dest / "qt.conf").write_text("[Paths]\nPlugins = plugins\n")


def pack_windows(build: Path, dest: Path, qt_bin):
    shutil.copy(build / "client/minimax.exe", dest / "minimax.exe")
    shutil.copy(build / "updater/update.exe", dest / "update.exe")
    dep = find_tool("windeployqt", qt_bin) if qt_bin else shutil.which("windeployqt") or shutil.which("windeployqt6") or sys.exit("windeployqt not found")
    run(dep, "--no-translations", "--no-system-d3d-compiler", "--no-opengl-sw", dest / "minimax.exe")
    run(dep, "--no-translations", dest / "update.exe")
    # MinGW runtime DLLs next to the exe (windeployqt copies them only when it finds the compiler dir)
    gcc = shutil.which("g++") or shutil.which("gcc")
    if gcc:
        for name in ("libgcc_s_seh-1.dll", "libgcc_s_dw2-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll"):
            p = Path(gcc).parent / name
            if p.exists() and not (dest / name).exists():
                shutil.copy(p, dest / name)


def pack_macos(build: Path, dest: Path, qt_bin, icon):
    app = build / "client/minimax.app"
    shutil.copytree(app, dest / "minimax.app", symlinks=True)
    shutil.copy(build / "updater/update", dest / "update")
    run(find_tool("macdeployqt", qt_bin), dest / "minimax.app")
    # the updater uses the frameworks bundled inside the app
    run("install_name_tool", "-add_rpath", "@executable_path/minimax.app/Contents/Frameworks", dest / "update")
    if icon and icon.exists():
        try:
            from PIL import Image
            res = dest / "minimax.app/Contents/Resources"; res.mkdir(parents=True, exist_ok=True)
            Image.open(icon).convert("RGBA").resize((512, 512)).save(res / "logo.icns")
            import plistlib
            pl = dest / "minimax.app/Contents/Info.plist"
            d = plistlib.loads(pl.read_bytes()); d["CFBundleIconFile"] = "logo"; pl.write_bytes(plistlib.dumps(d))
        except Exception as e:  # icon is cosmetic
            print("icon conversion skipped:", e)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", required=True)
    ap.add_argument("--platform", required=True)
    ap.add_argument("--out", default="dist")
    ap.add_argument("--qt-bin")
    a = ap.parse_args()
    build, out = Path(a.build).resolve(), Path(a.out).resolve()
    dest = out / a.platform
    shutil.rmtree(dest, ignore_errors=True); dest.mkdir(parents=True)
    if "Win" in a.platform:
        pack_windows(build, dest, a.qt_bin)
    elif "Mac" in a.platform:
        pack_macos(build, dest, a.qt_bin, ROOT / "logo.ico")
    else:
        pack_linux(build, dest, a.qt_bin)
    prepare_configs(build, dest)
    bad = [p for p in dest.rglob("*") if p.name.lower() == "captchaapi.cfg"]
    if bad:
        sys.exit(f"CaptchaAPI.cfg must not be shipped: {bad}")
    if "Win" in a.platform:
        arc = out / f"{a.platform}.zip"
        with zipfile.ZipFile(arc, "w", zipfile.ZIP_DEFLATED) as z:
            for p in dest.rglob("*"):
                z.write(p, Path(a.platform) / p.relative_to(dest))
    else:
        arc = out / f"{a.platform}.tar.gz"
        with tarfile.open(arc, "w:gz") as t:
            t.add(dest, arcname=a.platform)
    print("archive:", arc, f"({arc.stat().st_size // 1024} KiB)")


if __name__ == "__main__":
    main()
