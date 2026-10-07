#!/usr/bin/env python3
"""Build a flat release archive that contains only runtime shared libraries.

The CMake target directory on Windows is a multi-config build tree. Publishing
that tree with ``tar -C <artifact> .`` ships ``CMakeFiles/``, ``.vcxproj``,
import libraries (``.lib`` / ``.exp``), object files, and other build junk.
Consumers need the plugin and the shared libraries it actually loads, with no
extra nesting.

Windows DLLs that the staged binaries import, but that vcpkg applocal left only
under ``build/vcpkg_installed/<triplet>/bin``, are copied in as well. DLLs that
are merely installed and never imported (for example ``libssl`` when curl is
built with Schannel) are left out.
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import sys
import tarfile
from pathlib import Path, PureWindowsPath


# Prefer the Release output when the same basename exists in more than one
# MSVC config directory. Single-config generators (Ninja) have no config
# directory and sit between Release and Debug.
_CONFIG_RANK = {
    "release": 30,
    "relwithdebinfo": 20,
    "minsizerel": 10,
    "debug": 0,
}
_UNCONFIGURED_RANK = 15


def is_shared_library_name(name: str) -> bool:
    """True for runtime shared libraries, not import libs or static archives."""
    lower = name.lower()
    if lower.endswith(".dll") or lower.endswith(".dylib"):
        return True
    stem, sep, rest = lower.rpartition(".so")
    if not sep or not stem:
        return False
    if rest == "":
        return True
    if not rest.startswith("."):
        return False
    version = rest[1:]
    return bool(version) and all(part.isdigit() for part in version.split("."))


def _collision_key(name: str) -> str:
    lower = name.lower()
    if lower.endswith(".dll") or lower.endswith(".dylib"):
        return lower
    return name


def _path_rank(path: Path) -> tuple[int, float]:
    rank = _UNCONFIGURED_RANK
    for part in path.parts:
        configured = _CONFIG_RANK.get(part.lower())
        if configured is not None:
            rank = configured
            break
    try:
        mtime = path.stat().st_mtime
    except OSError:
        mtime = 0.0
    return (rank, mtime)


def iter_shared_libraries(root: Path) -> list[Path]:
    found: list[Path] = []
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in filenames:
            if not is_shared_library_name(name):
                continue
            path = Path(dirpath) / name
            if path.is_file():
                found.append(path)
    return found


def choose_libraries(paths: list[Path]) -> dict[str, Path]:
    """One file per archive basename. Release wins over Debug and loose copies."""
    chosen: dict[str, Path] = {}
    ranks: dict[str, tuple[int, float]] = {}
    for path in paths:
        key = _collision_key(path.name)
        rank = _path_rank(path)
        if key not in chosen or rank > ranks[key]:
            chosen[key] = path
            ranks[key] = rank
    return chosen


def vcpkg_release_bin_dirs(artifact_path: Path) -> list[Path]:
    installed = artifact_path.parent / "vcpkg_installed"
    if not installed.is_dir():
        return []
    bins: list[Path] = []
    for child in sorted(installed.iterdir()):
        bin_dir = child / "bin"
        if bin_dir.is_dir():
            bins.append(bin_dir)
    return bins


def index_vcpkg_dlls(bin_dirs: list[Path]) -> dict[str, Path]:
    found: dict[str, Path] = {}
    for bin_dir in bin_dirs:
        for pattern in ("*.dll", "*.DLL"):
            for path in sorted(bin_dir.glob(pattern)):
                if path.is_file():
                    found.setdefault(path.name.lower(), path)
    return found


def _rva_to_offset(
    sections: list[tuple[int, int, int, int]],
    rva: int,
) -> int | None:
    for virtual_address, virtual_size, raw_ptr, raw_size in sections:
        span = max(virtual_size, raw_size)
        if virtual_address <= rva < virtual_address + max(span, 1):
            delta = rva - virtual_address
            if delta < raw_size:
                return raw_ptr + delta
    return None


def _cstring(data: bytes, offset: int | None) -> str | None:
    if offset is None or offset < 0 or offset >= len(data):
        return None
    end = data.find(b"\x00", offset)
    if end < 0:
        return None
    raw = data[offset:end]
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError:
        return None
    text = text.strip()
    if not text:
        return None
    return PureWindowsPath(text).name


def _read_sections(
    data: bytes,
    section_off: int,
    count: int,
) -> list[tuple[int, int, int, int]]:
    sections: list[tuple[int, int, int, int]] = []
    for index in range(count):
        off = section_off + index * 40
        if off + 40 > len(data):
            break
        _name, virtual_size, virtual_address, raw_size, raw_ptr = struct.unpack_from(
            "<8sIIII",
            data,
            off,
        )
        sections.append((virtual_address, virtual_size, raw_ptr, raw_size))
    return sections


def imported_dll_names(path: Path) -> list[str]:
    """DLL names from a PE import table and delay-load directory.

    Non-PE files (``.so`` / ``.dylib``) and truncated images yield an empty
    list so packaging can still archive the plugin itself.
    """
    try:
        data = path.read_bytes()
    except OSError:
        return []
    if len(data) < 64 or data[:2] != b"MZ":
        return []
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if e_lfanew <= 0 or e_lfanew + 24 > len(data):
        return []
    if data[e_lfanew : e_lfanew + 4] != b"PE\0\0":
        return []
    coff = e_lfanew + 4
    _machine, nsections, _timestamp, _symptr, _nsyms, opt_size, _chars = (
        struct.unpack_from("<HHIIIHH", data, coff)
    )
    opt = coff + 20
    if opt_size < 2 or opt + opt_size > len(data):
        return []
    magic = struct.unpack_from("<H", data, opt)[0]
    if magic == 0x10B:
        dir_off = opt + 96
    elif magic == 0x20B:
        dir_off = opt + 112
    else:
        return []
    if dir_off > opt + opt_size or dir_off < 4:
        return []
    dir_count = struct.unpack_from("<I", data, dir_off - 4)[0]
    dir_count = max(0, min(dir_count, 16))

    def directory(index: int) -> tuple[int, int] | None:
        if index >= dir_count:
            return None
        off = dir_off + index * 8
        if off + 8 > len(data):
            return None
        return struct.unpack_from("<II", data, off)

    sections = _read_sections(data, opt + opt_size, nsections)
    names: list[str] = []
    seen: set[str] = set()

    def add(name: str | None) -> None:
        if not name:
            return
        key = name.lower()
        if key in seen:
            return
        seen.add(key)
        names.append(name)

    import_dir = directory(1)
    if import_dir and import_dir[0]:
        cursor = _rva_to_offset(sections, import_dir[0])
        # IMAGE_IMPORT_DESCRIPTOR is 20 bytes; Name RVA is at +12.
        for _ in range(4096):
            if cursor is None or cursor + 20 > len(data):
                break
            descriptor = data[cursor : cursor + 20]
            if descriptor == b"\x00" * 20:
                break
            name_rva = struct.unpack_from("<I", descriptor, 12)[0]
            add(_cstring(data, _rva_to_offset(sections, name_rva)))
            cursor += 20

    delay_dir = directory(13)
    if delay_dir and delay_dir[0]:
        cursor = _rva_to_offset(sections, delay_dir[0])
        # ImgDelayDescr is 32 bytes; DllNameRVA is at +4.
        for _ in range(4096):
            if cursor is None or cursor + 32 > len(data):
                break
            descriptor = data[cursor : cursor + 32]
            if descriptor == b"\x00" * 32:
                break
            attributes = struct.unpack_from("<I", descriptor, 0)[0]
            # Bit 0 (dlattrRva) means the fields are RVAs. Older images store
            # absolute VAs; skip those rather than treat a VA as an RVA.
            if attributes & 1 == 0:
                break
            name_rva = struct.unpack_from("<I", descriptor, 4)[0]
            if name_rva == 0:
                break
            add(_cstring(data, _rva_to_offset(sections, name_rva)))
            cursor += 32

    return names


def add_transitive_windows_dlls(
    selected: dict[str, Path],
    vcpkg_dlls: dict[str, Path],
) -> dict[str, Path]:
    staged = dict(selected)
    pending = [path for path in staged.values() if path.name.lower().endswith(".dll")]
    seen: set[str] = set()
    while pending:
        current = pending.pop()
        try:
            marker = str(current.resolve())
        except OSError:
            marker = str(current)
        if marker in seen:
            continue
        seen.add(marker)
        for dep in imported_dll_names(current):
            key = dep.lower()
            if key in staged:
                continue
            source = vcpkg_dlls.get(key)
            if source is None:
                continue
            staged[key] = source
            pending.append(source)
    return staged


def plugin_library_present(names: list[str], stem: str) -> bool:
    lowered = {name.lower() for name in names}
    exact = {
        f"{stem}.dll".lower(),
        f"{stem}.so".lower(),
        f"lib{stem}.so".lower(),
        f"{stem}.dylib".lower(),
        f"lib{stem}.dylib".lower(),
    }
    if lowered & exact:
        return True
    prefixes = (f"lib{stem}.so.".lower(), f"{stem}.so.".lower())
    return any(name.startswith(prefixes) for name in lowered)


def stage_and_archive(
    artifact_path: Path,
    plugin_stem: str,
    staging: Path,
    archive: Path,
) -> list[str]:
    if not artifact_path.is_dir():
        raise SystemExit(f"artifact path does not exist: {artifact_path}")
    if not plugin_stem:
        raise SystemExit("plugin stem is empty")

    selected = choose_libraries(iter_shared_libraries(artifact_path))
    vcpkg_dlls = index_vcpkg_dlls(vcpkg_release_bin_dirs(artifact_path))
    selected = add_transitive_windows_dlls(selected, vcpkg_dlls)

    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)

    archived_names: list[str] = []
    for path in selected.values():
        destination = staging / path.name
        shutil.copy2(path, destination)
        archived_names.append(destination.name)
    archived_names.sort(key=str.lower)

    if not archived_names:
        raise SystemExit(
            f"no shared libraries found under {artifact_path}"
        )
    if not plugin_library_present(archived_names, plugin_stem):
        raise SystemExit(
            "plugin shared library "
            f"{plugin_stem} (.dll / .so / .dylib) was not found under {artifact_path}; "
            f"staged: {', '.join(archived_names)}"
        )

    archive.parent.mkdir(parents=True, exist_ok=True)
    if archive.exists():
        archive.unlink()
    with tarfile.open(archive, "w:gz") as tar:
        for name in archived_names:
            tar.add(staging / name, arcname=name, recursive=False)
    return archived_names


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifact-path", required=True, type=Path)
    parser.add_argument("--plugin-stem", required=True)
    parser.add_argument("--staging", required=True, type=Path)
    parser.add_argument("--archive", required=True, type=Path)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    names = stage_and_archive(
        args.artifact_path,
        args.plugin_stem,
        args.staging,
        args.archive,
    )
    print(f"Packaged {len(names)} shared libraries into {args.archive}:")
    for name in names:
        print(f"  {name}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SystemExit:
        raise
    except Exception as exc:  # noqa: BLE001 — surface a single CI error line
        print(f"failed to package shared libraries: {exc}", file=sys.stderr)
        raise SystemExit(1) from exc
