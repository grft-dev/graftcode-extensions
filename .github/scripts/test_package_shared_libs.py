#!/usr/bin/env python3
"""Fixture checks for the shared-library release packager."""

from __future__ import annotations

import struct
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import package_shared_libs as packager


def _pe_headers(
    *,
    magic: int,
    import_rva: int,
    import_size: int,
    delay_rva: int = 0,
    delay_size: int = 0,
) -> tuple[bytes, int]:
    """Return (header bytes padded to 0x200, SizeOfOptionalHeader)."""
    dos = bytearray(64)
    dos[0:2] = b"MZ"
    struct.pack_into("<I", dos, 0x3C, 64)

    if magic == 0x10B:
        opt_size = 96 + 16 * 8
        number_off = 92
    elif magic == 0x20B:
        opt_size = 112 + 16 * 8
        number_off = 108
    else:
        raise AssertionError(magic)

    optional = bytearray(opt_size)
    struct.pack_into("<H", optional, 0, magic)
    align_off = 32
    struct.pack_into("<I", optional, align_off, 0x1000)
    struct.pack_into("<I", optional, align_off + 4, 0x200)
    # SizeOfImage and SizeOfHeaders share these offsets on PE32 and PE32+.
    struct.pack_into("<I", optional, 56, 0x2000)
    struct.pack_into("<I", optional, 60, 0x200)
    struct.pack_into("<I", optional, number_off, 16)
    struct.pack_into("<II", optional, number_off + 4 + 8, import_rva, import_size)
    if delay_rva:
        struct.pack_into(
            "<II",
            optional,
            number_off + 4 + 13 * 8,
            delay_rva,
            delay_size,
        )

    coff = struct.pack(
        "<HHIIIHH",
        0x14C if magic == 0x10B else 0x8664,
        1,
        0,
        0,
        0,
        opt_size,
        0x2102,
    )
    section = struct.pack(
        "<8sIIIIIIHHI",
        b".rdata\0\0",
        0x100,
        0x1000,
        0x200,
        0x200,
        0,
        0,
        0,
        0,
        0x40000040,
    )
    headers = bytes(dos) + b"PE\0\0" + coff + bytes(optional) + section
    if len(headers) > 0x200:
        raise AssertionError(f"headers are {len(headers)} bytes")
    return headers.ljust(0x200, b"\x00"), opt_size


def make_pe_dll(
    imports: list[str],
    delay_imports: list[str] | None = None,
    *,
    pe32_plus: bool = False,
) -> bytes:
    """Minimal PE image whose import directory lists ``imports``."""
    delay_imports = delay_imports or []
    blob = bytearray()
    descriptors = bytearray()
    for _name in imports:
        descriptors += b"\x00" * 12 + b"\x00\x00\x00\x00" + b"\x00" * 4
    descriptors += b"\x00" * 20
    delay = bytearray()
    for _name in delay_imports:
        delay += b"\x01\x00\x00\x00" + b"\x00" * 28
    if delay_imports:
        delay += b"\x00" * 32

    strings_at = len(descriptors) + len(delay)
    names = bytearray()
    import_name_rvas: list[int] = []
    for name in imports:
        import_name_rvas.append(0x1000 + strings_at + len(names))
        names += name.encode("ascii") + b"\x00"
    delay_name_rvas: list[int] = []
    for name in delay_imports:
        delay_name_rvas.append(0x1000 + strings_at + len(names))
        names += name.encode("ascii") + b"\x00"

    for index, rva in enumerate(import_name_rvas):
        struct.pack_into("<I", descriptors, index * 20 + 12, rva)
    for index, rva in enumerate(delay_name_rvas):
        struct.pack_into("<I", delay, index * 32 + 4, rva)

    section = bytes(descriptors) + bytes(delay) + bytes(names)
    section = section.ljust(0x200, b"\x00")
    headers, _opt_size = _pe_headers(
        magic=0x20B if pe32_plus else 0x10B,
        import_rva=0x1000,
        import_size=len(descriptors),
        delay_rva=0x1000 + len(descriptors) if delay_imports else 0,
        delay_size=len(delay) if delay_imports else 0,
    )
    return headers + section


class NameTests(unittest.TestCase):
    def test_shared_library_names(self) -> None:
        self.assertTrue(packager.is_shared_library_name("PubSubPlugin.dll"))
        self.assertTrue(packager.is_shared_library_name("libPubSubPlugin.so"))
        self.assertTrue(packager.is_shared_library_name("libssl.so.3"))
        self.assertTrue(packager.is_shared_library_name("libcrypto.so.3.0.0"))
        self.assertTrue(packager.is_shared_library_name("libPubSubPlugin.dylib"))
        self.assertFalse(packager.is_shared_library_name("PubSubPlugin.lib"))
        self.assertFalse(packager.is_shared_library_name("PubSubPlugin.exp"))
        self.assertFalse(packager.is_shared_library_name("PubSubPlugin.pdb"))
        self.assertFalse(packager.is_shared_library_name("foo.obj"))
        self.assertFalse(packager.is_shared_library_name("libPubSubPlugin.so.bak"))
        self.assertFalse(packager.is_shared_library_name("CMakeCXXCompilerId.exe"))


class PeImportTests(unittest.TestCase):
    def test_pe32_and_pe32_plus_imports(self) -> None:
        for pe32_plus in (False, True):
            blob = make_pe_dll(
                [r".\libcurl.dll", "Z.DLL"],
                ["libssl-3-x64.dll"],
                pe32_plus=pe32_plus,
            )
            with tempfile.TemporaryDirectory() as tmp:
                path = Path(tmp) / "plugin.dll"
                path.write_bytes(blob)
                self.assertEqual(
                    [name.lower() for name in packager.imported_dll_names(path)],
                    ["libcurl.dll", "z.dll", "libssl-3-x64.dll"],
                )

    def test_non_pe_is_ignored(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "libPubSubPlugin.so"
            path.write_bytes(b"\x7fELF not a real object")
            self.assertEqual(packager.imported_dll_names(path), [])


class PackageTests(unittest.TestCase):
    def test_windows_tree_is_flat_runtime_dlls_only(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            artifact = root / "pubsub" / "build" / "PubSubPlugin"
            release = artifact / "Release"
            debug = artifact / "Debug"
            release.mkdir(parents=True)
            debug.mkdir()
            (artifact / "CMakeFiles").mkdir()
            (artifact / "CMakeFiles" / "cmake.check_cache").write_text("x")
            (artifact / "PubSubPlugin.dir" / "Release").mkdir(parents=True)
            (artifact / "PubSubPlugin.dir" / "Release" / "PubSubUtil.obj").write_bytes(b"obj")
            (artifact / "INSTALL.dir").mkdir()
            (artifact / "RUN_TESTS.dir").mkdir()
            (artifact / "PubSubPlugin.vcxproj").write_text("<Project/>")
            (artifact / "PubSubPlugin.vcxproj.filters").write_text("<Project/>")
            (artifact / "cmake_install.cmake").write_text("install()")
            (artifact / "CTestTestfile.cmake").write_text("add_test()")
            (artifact / "PubSubPlugin.tlog").write_text("log")

            plugin = make_pe_dll([r"libcurl.dll", "extra.dll"])
            (release / "PubSubPlugin.dll").write_bytes(plugin)
            (release / "PubSubPlugin.exp").write_bytes(b"exp")
            (release / "PubSubPlugin.lib").write_bytes(b"lib")
            (release / "PubSubPlugin.pdb").write_bytes(b"pdb")
            (release / "libcurl.dll").write_bytes(b"MZ not parsed further")
            (release / "libcrypto-3-x64.dll").write_bytes(b"crypto")
            (release / "z.dll").write_bytes(b"zlib")
            # Newer Debug copy of the same basename must not win.
            (debug / "PubSubPlugin.dll").write_bytes(b"debug-plugin")

            vcpkg_bin = (
                root / "pubsub" / "build" / "vcpkg_installed" / "x64-windows" / "bin"
            )
            vcpkg_bin.mkdir(parents=True)
            (vcpkg_bin / "extra.dll").write_bytes(b"transitive")
            (vcpkg_bin / "libssl-3-x64.dll").write_bytes(b"unused-tls")
            debug_bin = (
                root
                / "pubsub"
                / "build"
                / "vcpkg_installed"
                / "x64-windows"
                / "debug"
                / "bin"
            )
            debug_bin.mkdir(parents=True)
            (debug_bin / "extra.dll").write_bytes(b"debug-extra")

            staging = root / "package-staging"
            archive = root / "release-payloads" / "pubsub-windows-x86_64.tar.gz"
            names = packager.stage_and_archive(
                artifact,
                "PubSubPlugin",
                staging,
                archive,
            )
            self.assertEqual(
                names,
                [
                    "extra.dll",
                    "libcrypto-3-x64.dll",
                    "libcurl.dll",
                    "PubSubPlugin.dll",
                    "z.dll",
                ],
            )
            self.assertEqual((staging / "PubSubPlugin.dll").read_bytes(), plugin)
            self.assertEqual((staging / "extra.dll").read_bytes(), b"transitive")
            with tarfile.open(archive, "r:gz") as tar:
                self.assertEqual(tar.getnames(), names)
                for member in tar.getmembers():
                    self.assertFalse(member.isdir())
                    self.assertFalse("/" in member.name or "\\" in member.name)

    def test_imported_libssl_is_taken_from_vcpkg_bin(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            artifact = root / "build" / "PubSubPlugin" / "Release"
            artifact.mkdir(parents=True)
            (artifact / "PubSubPlugin.dll").write_bytes(
                make_pe_dll(["libssl-3-x64.dll"])
            )
            vcpkg_bin = root / "build" / "vcpkg_installed" / "x64-windows" / "bin"
            vcpkg_bin.mkdir(parents=True)
            (vcpkg_bin / "libssl-3-x64.dll").write_bytes(b"ssl")
            names = packager.stage_and_archive(
                artifact.parent,
                "PubSubPlugin",
                root / "staging",
                root / "out.tar.gz",
            )
            self.assertEqual(names, ["libssl-3-x64.dll", "PubSubPlugin.dll"])

    def test_linux_tree_keeps_plugin_and_shipped_so_deps_only(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            artifact = root / "pubsub" / "build" / "PubSubPlugin"
            artifact.mkdir(parents=True)
            (artifact / "CMakeFiles").mkdir()
            (artifact / "cmake_install.cmake").write_text("install()")
            plugin = artifact / "libPubSubPlugin.so"
            plugin.write_bytes(b"plugin-so")
            plugin.chmod(0o755)
            real_dep = artifact / "libcrypto.so.3"
            real_dep.write_bytes(b"crypto-so")
            link = artifact / "libcrypto.so"
            link.symlink_to(real_dep.name)

            staging = root / "package-staging"
            archive = root / "out.tar.gz"
            names = packager.stage_and_archive(
                artifact,
                "PubSubPlugin",
                staging,
                archive,
            )
            self.assertEqual(names, ["libcrypto.so", "libcrypto.so.3", "libPubSubPlugin.so"])
            self.assertEqual((staging / "libcrypto.so").read_bytes(), b"crypto-so")
            self.assertTrue((staging / "libPubSubPlugin.so").stat().st_mode & 0o111)
            with tarfile.open(archive, "r:gz") as tar:
                self.assertEqual(tar.getnames(), names)

    def test_missing_plugin_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            artifact = root / "build" / "PubSubPlugin" / "Release"
            artifact.mkdir(parents=True)
            (artifact / "libcurl.dll").write_bytes(b"curl")
            with self.assertRaises(SystemExit) as raised:
                packager.stage_and_archive(
                    artifact.parent,
                    "PubSubPlugin",
                    root / "staging",
                    root / "out.tar.gz",
                )
            self.assertIn("plugin shared library", str(raised.exception))

    def test_empty_tree_fails(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            artifact = root / "build" / "PubSubPlugin"
            artifact.mkdir(parents=True)
            (artifact / "CMakeFiles").mkdir()
            (artifact / "cmake_install.cmake").write_text("x")
            with self.assertRaises(SystemExit) as raised:
                packager.stage_and_archive(
                    artifact,
                    "PubSubPlugin",
                    root / "staging",
                    root / "out.tar.gz",
                )
            self.assertIn("no shared libraries", str(raised.exception))


if __name__ == "__main__":
    unittest.main()
