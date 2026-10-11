#!/usr/bin/env python3
"""Builds and signs an update manifest for one client folder (what the server's "upload update" does).

  python3 tools/make_manifest.py --gen-keys keys/          # once: creates update_private.key / update_public.key
  python3 tools/make_manifest.py DIR --platform minimaxLinuxX64 --key keys/update_private.key --out OUTDIR

OUTDIR gets manifest.json + manifest.sig (serve them at /update/<platform>/); the files of DIR are
served from /update/<platform>/files/<relative path>. Put update_public.key into configs/ of the client
before building it (the updater refuses unsigned or wrongly signed manifests).
"""
import argparse, base64, hashlib, json, os, sys, time
from pathlib import Path

try:
    from nacl import signing
except ImportError:
    sys.exit("pip install pynacl")

SKIP = {"configs/connect.ip"}


def gen_keys(d: Path):
    d.mkdir(parents=True, exist_ok=True)
    sk = signing.SigningKey.generate()
    (d / "update_private.key").write_text(base64.b64encode(bytes(sk)).decode())
    (d / "update_public.key").write_text(base64.b64encode(bytes(sk.verify_key)).decode())
    print("keys written to", d, "- keep update_private.key secret on the server")


def build(src: Path, platform: str, removed):
    files = []
    for p in sorted(src.rglob("*")):
        if p.is_file() and not p.name.endswith((".old", ".mmnew")):
            rel = p.relative_to(src).as_posix()
            if rel in SKIP:
                continue
            files.append({"path": rel, "sha256": hashlib.sha256(p.read_bytes()).hexdigest(), "size": p.stat().st_size})
    rev = hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()[:16]
    return {"platform": platform, "revision": rev, "created": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "files": files, "removed": removed}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir", nargs="?")
    ap.add_argument("--gen-keys")
    ap.add_argument("--platform")
    ap.add_argument("--key")
    ap.add_argument("--out")
    ap.add_argument("--removed", nargs="*", default=[])
    a = ap.parse_args()
    if a.gen_keys:
        return gen_keys(Path(a.gen_keys))
    if not (a.dir and a.platform and a.key and a.out):
        ap.error("dir, --platform, --key and --out are required")
    m = json.dumps(build(Path(a.dir), a.platform, a.removed), indent=1).encode()
    sk = signing.SigningKey(base64.b64decode(Path(a.key).read_text().strip()))
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "manifest.json").write_bytes(m)
    (out / "manifest.sig").write_text(base64.b64encode(sk.sign(m).signature).decode())
    print("manifest written to", out)


if __name__ == "__main__":
    main()
