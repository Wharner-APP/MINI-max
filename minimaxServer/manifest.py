"""Builds + signs the update manifest (same format as tools/make_manifest.py of the client repo)."""
import base64, hashlib, json, shutil, subprocess, time
from pathlib import Path

SKIP = {"configs/connect.ip"}


def build(src: Path, platform: str, removed=()):
    files = []
    for p in sorted(src.rglob("*")):
        if p.is_file() and not p.name.endswith((".old", ".mmnew")):
            rel = p.relative_to(src).as_posix()
            if rel not in SKIP:
                files.append({"path": rel, "sha256": hashlib.sha256(p.read_bytes()).hexdigest(), "size": p.stat().st_size})
    rev = hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()[:16]
    return {"platform": platform, "revision": rev, "created": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "files": files, "removed": list(removed)}


def gen_keys(d: Path):
    from nacl import signing
    d.mkdir(parents=True, exist_ok=True)
    sk = signing.SigningKey.generate()
    (d / "update_private.key").write_text(base64.b64encode(bytes(sk)).decode())
    (d / "update_public.key").write_text(base64.b64encode(bytes(sk.verify_key)).decode())


def sign(data: bytes, private_key_file: Path) -> str:
    exe = Path(__file__).parent / "bin" / "mmsign.exe"      # C++ signer (built by CMake) is preferred
    if exe.exists():
        tmp = private_key_file.parent / "_m.bin"
        tmp.write_bytes(data)
        out = subprocess.run([str(exe), "sign", str(private_key_file), str(tmp)], capture_output=True, text=True, check=True)
        tmp.unlink()
        return out.stdout.strip()
    from nacl import signing
    sk = signing.SigningKey(base64.b64decode(private_key_file.read_text().strip()))
    return base64.b64encode(sk.sign(data).signature).decode()


def publish(src: Path, platform: str, dest_root: Path, private_key_file: Path):
    dest = dest_root / platform
    files_dir = dest / "files"
    if files_dir.exists():
        shutil.rmtree(files_dir)
    shutil.copytree(src, files_dir, ignore=shutil.ignore_patterns("connect.ip", "*.old", "*.mmnew"))
    m = build(src, platform)
    raw = json.dumps(m, indent=1).encode()
    (dest / "manifest.json").write_bytes(raw)
    (dest / "manifest.sig").write_text(sign(raw, private_key_file))
    return m["revision"], len(m["files"])
