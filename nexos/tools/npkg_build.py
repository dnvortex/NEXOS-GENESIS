#!/usr/bin/env python3
"""
npkg_build.py — NexOS Package Builder v1.0
Build .npkg packages from source directories and embed them in the kernel.

Usage:
  Build a single package:
    python3 tools/npkg_build.py build packages/hello/ [-o build/hello.npkg]

  Build all packages in packages/:
    python3 tools/npkg_build.py build-all

  Generate pkg_store.c (embed packages into kernel):
    python3 tools/npkg_build.py genstore

  Show package info:
    python3 tools/npkg_build.py info build/hello.npkg

  List files in a package:
    python3 tools/npkg_build.py list build/hello.npkg
"""

import os, sys, struct, pathlib, textwrap, time

# ── Binary format constants (must match npkg.h) ─────────────────────────────
NPKG_MAGIC        = 0x4B50584E   # "NXPK"
NPKG_FORMAT_VER   = 1
NPKG_NAME_MAX     = 64
NPKG_VER_MAX      = 32
NPKG_DESC_MAX     = 128
NPKG_AUTH_MAX     = 64
NPKG_PATH_MAX     = 256
NPKG_HEADER_SIZE  = 320          # 4+2+2+64+32+128+64+4+4+16
NPKG_ENTRY_SIZE   = 260          # 256 path + 4 size

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT  = os.path.normpath(os.path.join(SCRIPT_DIR, ".."))
PKG_DIR    = os.path.join(REPO_ROOT, "packages")
BUILD_DIR  = os.path.join(REPO_ROOT, "build", "packages")
STORE_C    = os.path.join(REPO_ROOT, "kernel", "pkg", "pkg_store.c")

# ── Helper: fixed-size null-padded byte string ───────────────────────────────
def fixed(s: str, n: int) -> bytes:
    b = s.encode("utf-8")[:n-1]
    return b + b"\x00" * (n - len(b))

# ── Read PKGINFO ─────────────────────────────────────────────────────────────
def read_pkginfo(pkg_dir: str) -> dict:
    path = os.path.join(pkg_dir, "PKGINFO")
    if not os.path.exists(path):
        raise FileNotFoundError(f"PKGINFO not found in {pkg_dir}")
    info = {"Name": "", "Version": "1.0.0", "Description": "",
            "Author": "", "License": "MIT", "InstallScript": ""}
    with open(path) as f:
        for ln in f:
            ln = ln.strip()
            if ":" in ln:
                k, _, v = ln.partition(":")
                info[k.strip()] = v.strip()
    return info

# ── Collect files ─────────────────────────────────────────────────────────────
def collect_files(pkg_dir: str, install_script_name: str) -> list[tuple[str,bytes]]:
    """Return list of (vfs_path, data) for all package files (not PKGINFO)."""
    skip = {"PKGINFO", install_script_name}
    files = []
    for root, dirs, names in os.walk(pkg_dir):
        dirs.sort()
        for name in sorted(names):
            if name in skip:
                continue
            abs_path  = os.path.join(root, name)
            # vfs_path: strip pkg_dir prefix, keep everything after
            rel = os.path.relpath(abs_path, pkg_dir)
            vfs_path = "/" + rel.replace(os.sep, "/")
            with open(abs_path, "rb") as f:
                data = f.read()
            files.append((vfs_path, data))
    return files

# ── Pack header ──────────────────────────────────────────────────────────────
def pack_header(info: dict, file_count: int, install_script_size: int) -> bytes:
    hdr = struct.pack(
        "<IHH",
        NPKG_MAGIC,
        NPKG_FORMAT_VER,
        0,                              # flags
    )
    hdr += fixed(info["Name"],        NPKG_NAME_MAX)
    hdr += fixed(info["Version"],     NPKG_VER_MAX)
    hdr += fixed(info["Description"], NPKG_DESC_MAX)
    hdr += fixed(info["Author"],      NPKG_AUTH_MAX)
    hdr += struct.pack("<II", file_count, install_script_size)
    hdr += b"\x00" * 16               # reserved
    assert len(hdr) == NPKG_HEADER_SIZE, f"Header size mismatch: {len(hdr)}"
    return hdr

# ── Build package ─────────────────────────────────────────────────────────────
def build_package(pkg_dir: str, out_path: str | None = None) -> str:
    pkg_dir  = os.path.abspath(pkg_dir)
    pkg_name = os.path.basename(pkg_dir)
    info     = read_pkginfo(pkg_dir)

    os.makedirs(BUILD_DIR, exist_ok=True)
    if out_path is None:
        out_path = os.path.join(BUILD_DIR, f"{info['Name']}.npkg")

    files  = collect_files(pkg_dir, info.get("InstallScript", ""))
    script = b""
    script_name = info.get("InstallScript", "").strip()
    if script_name:
        sp = os.path.join(pkg_dir, script_name)
        if os.path.exists(sp):
            with open(sp, "rb") as f:
                script = f.read()

    header = pack_header(info, len(files), len(script))

    with open(out_path, "wb") as out:
        out.write(header)
        for vfs_path, data in files:
            entry  = fixed(vfs_path, NPKG_PATH_MAX)
            entry += struct.pack("<I", len(data))
            out.write(entry)
            out.write(data)
        if script:
            out.write(script)

    total = os.path.getsize(out_path)
    print(f"  [built]  {info['Name']} v{info['Version']}"
          f"  ({len(files)} files, {total} bytes)  →  {out_path}")
    return out_path

# ── Build all packages ────────────────────────────────────────────────────────
def build_all() -> list[str]:
    if not os.path.isdir(PKG_DIR):
        print(f"[npkg] No packages directory found at {PKG_DIR}")
        return []
    built = []
    for entry in sorted(os.scandir(PKG_DIR), key=lambda e: e.name):
        if entry.is_dir() and os.path.exists(os.path.join(entry.path, "PKGINFO")):
            try:
                path = build_package(entry.path)
                built.append(path)
            except Exception as e:
                print(f"  [ERROR] {entry.name}: {e}")
    return built

# ── Generate pkg_store.c ──────────────────────────────────────────────────────
def genstore(pkg_paths: list[str] | None = None) -> None:
    if pkg_paths is None:
        pkg_paths = build_all()
    if not pkg_paths:
        print(f"[npkg] No packages to embed — writing empty store to {STORE_C}")
        with open(STORE_C, "w") as f:
            f.write(textwrap.dedent("""\
                /* AUTO-GENERATED by tools/npkg_build.py */
                #include "pkg_store.h"
                const npkg_store_entry_t npkg_store[] = {
                    { (void*)0, (void*)0, 0 }
                };
            """))
        return

    lines = ["/* AUTO-GENERATED by tools/npkg_build.py — do not edit */",
             '#include "pkg_store.h"',
             ""]

    store_entries = []
    for pkg_path in pkg_paths:
        var  = "_pkg_" + os.path.splitext(os.path.basename(pkg_path))[0]
        var  = var.replace("-", "_")
        with open(pkg_path, "rb") as f:
            data = f.read()
        # C byte array (16 bytes per row, every row ends with comma)
        rows  = []
        for i in range(0, len(data), 16):
            row = ",".join(f"0x{b:02X}" for b in data[i:i+16]) + ","
            rows.append("    " + row)
        lines.append(f"static const unsigned char {var}[] = {{")
        lines.extend(rows)
        lines.append("};")
        lines.append("")
        store_entries.append((os.path.basename(pkg_path), var))

    lines.append("const npkg_store_entry_t npkg_store[] = {")
    for fname, var in store_entries:
        lines.append(f'    {{ "{fname}", {var}, sizeof({var}) }},')
    lines.append("    { (void*)0, (void*)0, 0 }")
    lines.append("};")
    lines.append("")

    with open(STORE_C, "w") as f:
        f.write("\n".join(lines))

    total_bytes = sum(os.path.getsize(p) for p in pkg_paths)
    print(f"[npkg] Generated {STORE_C}")
    print(f"       Embedded {len(pkg_paths)} package(s), "
          f"{total_bytes} bytes total")

# ── Show package info ─────────────────────────────────────────────────────────
def cmd_info(npkg_path: str) -> None:
    with open(npkg_path, "rb") as f:
        raw = f.read()
    if len(raw) < NPKG_HEADER_SIZE:
        print("[error] File too small"); return
    magic, fver, flags = struct.unpack_from("<IHH", raw, 0)
    if magic != NPKG_MAGIC:
        print("[error] Not a valid .npkg file"); return
    off = 8
    name    = raw[off:off+NPKG_NAME_MAX].rstrip(b"\x00").decode()
    off += NPKG_NAME_MAX
    version = raw[off:off+NPKG_VER_MAX].rstrip(b"\x00").decode()
    off += NPKG_VER_MAX
    desc    = raw[off:off+NPKG_DESC_MAX].rstrip(b"\x00").decode()
    off += NPKG_DESC_MAX
    author  = raw[off:off+NPKG_AUTH_MAX].rstrip(b"\x00").decode()
    off += NPKG_AUTH_MAX
    n_files, script_sz = struct.unpack_from("<II", raw, off)
    print(f"Package:     {name}")
    print(f"Version:     {version}")
    print(f"Description: {desc}")
    print(f"Author:      {author}")
    print(f"Files:       {n_files}")
    print(f"Script:      {script_sz} bytes")
    print(f"Total size:  {len(raw)} bytes")

# ── List files in package ─────────────────────────────────────────────────────
def cmd_list(npkg_path: str) -> None:
    with open(npkg_path, "rb") as f:
        raw = f.read()
    if len(raw) < NPKG_HEADER_SIZE:
        print("[error] File too small"); return
    magic, fver, flags = struct.unpack_from("<IHH", raw, 0)
    if magic != NPKG_MAGIC:
        print("[error] Not a valid .npkg file"); return
    off = NPKG_HEADER_SIZE - 8  # skip to file_count
    off = 4 + 2 + 2 + NPKG_NAME_MAX + NPKG_VER_MAX + NPKG_DESC_MAX + NPKG_AUTH_MAX
    n_files, _ = struct.unpack_from("<II", raw, off)
    off = NPKG_HEADER_SIZE
    for i in range(n_files):
        path = raw[off:off+NPKG_PATH_MAX].rstrip(b"\x00").decode()
        size = struct.unpack_from("<I", raw, off + NPKG_PATH_MAX)[0]
        print(f"  {path}  ({size} bytes)")
        off += NPKG_ENTRY_SIZE + size

# ── CLI entry point ───────────────────────────────────────────────────────────
def main() -> None:
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        print(__doc__)
        return

    cmd = args[0]

    if cmd == "build":
        if len(args) < 2:
            print("Usage: npkg_build.py build <source_dir> [-o output.npkg]")
            sys.exit(1)
        out = None
        if len(args) >= 4 and args[2] == "-o":
            out = args[3]
        build_package(args[1], out)

    elif cmd == "build-all":
        built = build_all()
        if built:
            print(f"\n[npkg] Built {len(built)} package(s)")

    elif cmd == "genstore":
        genstore()

    elif cmd == "info":
        if len(args) < 2:
            print("Usage: npkg_build.py info <file.npkg>"); sys.exit(1)
        cmd_info(args[1])

    elif cmd == "list":
        if len(args) < 2:
            print("Usage: npkg_build.py list <file.npkg>"); sys.exit(1)
        cmd_list(args[1])

    else:
        print(f"Unknown command: {cmd}")
        print("Run with --help for usage.")
        sys.exit(1)

if __name__ == "__main__":
    main()
