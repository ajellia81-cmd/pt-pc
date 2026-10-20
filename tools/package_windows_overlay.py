#!/usr/bin/env python3
"""Overlay this port's executable and localization assets onto a stock Windows release.

The upstream Windows archive contains vendor runtime DLLs and DXIL shader blobs that
are not all reproducible from the public source checkout.  Starting from that archive
keeps the portable runtime byte-for-byte complete while allowing the game executable
and localized assets to be rebuilt from source.
"""

from __future__ import annotations

import argparse
import shutil
import tempfile
import zipfile
from pathlib import Path


def root_directory(names: list[str]) -> str:
    roots = {name.split("/", 1)[0] for name in names if "/" in name}
    if len(roots) != 1:
        raise RuntimeError("baseline archive must contain exactly one root directory")
    return next(iter(roots))


def copy_tree(source: Path, destination: Path) -> None:
    if source.exists():
        shutil.copytree(source, destination, dirs_exist_ok=True)


def require_baseline_features(files: set[str]) -> None:
    """Fail loudly if this is not the complete DX12/DLSS 5-capable upstream runtime."""
    required = {
        "D3D12/D3D12Core.dll",
        "pt-native-nvngx.dll",
        "nvngx_dlss.dll",
        "nvngx_dlssd.dll",
        "nvngx_dlssg.dll",
        "shaders/forward.frag.dxil",
    }
    missing = required - files
    if missing:
        raise RuntimeError("baseline lacks required original DX12/DLSS 5 files: " + ", ".join(sorted(missing)))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    exe = args.build / "pt_release.exe"
    if not exe.is_file():
        raise RuntimeError(f"missing rebuilt executable: {exe}")
    if not args.baseline.is_file():
        raise RuntimeError(f"missing upstream baseline archive: {args.baseline}")

    with tempfile.TemporaryDirectory(prefix="pt-windows-overlay-") as temporary:
        temp = Path(temporary)
        with zipfile.ZipFile(args.baseline) as archive:
            names = archive.namelist()
            archive.extractall(temp)
        root = root_directory(names)
        runtime = temp / root
        baseline_files = {
            path.relative_to(runtime).as_posix()
            for path in runtime.rglob("*")
            if path.is_file()
        }
        require_baseline_features(baseline_files)

        shutil.copy2(exe, runtime / "pt.exe")
        for sidecar in ("pt_release.exe.upscalers.json",):
            source = args.build / sidecar
            if source.is_file():
                shutil.copy2(source, runtime / sidecar.replace("pt_release", "pt"))
        copy_tree(args.build / "fonts", runtime / "fonts")
        copy_tree(args.build / "lisa_date", runtime / "lisa_date")
        copy_tree(args.build / "licenses", runtime / "licenses")

        missing = [entry for entry in baseline_files if not (runtime / entry).is_file()]
        if missing:
            raise RuntimeError("overlay removed baseline runtime files: " + ", ".join(sorted(missing)))

        args.out.mkdir(parents=True, exist_ok=True)
        archive_base = args.out / "P.T.PC.Port-windows-x64-Release"
        shutil.make_archive(str(archive_base), "zip", temp, root)


if __name__ == "__main__":
    main()
