#!/usr/bin/env python3
"""Overlay only the rebuilt executable and Korean-only assets onto a stock release.

The upstream Windows archive contains vendor runtime DLLs and DXIL shader blobs that
are not all reproducible from the public source checkout.  Starting from that archive
keeps the portable runtime byte-for-byte complete while allowing the game executable
and localized assets to be rebuilt from source.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import tempfile
import zipfile
from pathlib import Path


def root_directory(names: list[str]) -> str:
    roots = {name.split("/", 1)[0] for name in names if "/" in name}
    if len(roots) != 1:
        raise RuntimeError("baseline archive must contain exactly one root directory")
    return next(iter(roots))


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            hasher.update(chunk)
    return hasher.hexdigest()


def copy_required(source: Path, destination: Path) -> None:
    if not source.is_file():
        raise RuntimeError(f"missing required overlay file: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def copy_if_present(source: Path, destination: Path) -> None:
    if source.is_file():
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)


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
        baseline_files = {path.relative_to(runtime).as_posix(): digest(path)
                          for path in runtime.rglob("*") if path.is_file()}
        require_baseline_features(set(baseline_files))

        # Everything from the original release remains byte-identical except the
        # executable and the three Korean-localization files below.  In particular,
        # retain the original DX12, DLSS 5, shader and upscaler-manifest files.
        copy_required(exe, runtime / "pt.exe")
        copy_required(args.build / "fonts" / "NotoSansKR.ttf", runtime / "fonts" / "NotoSansKR.ttf")
        copy_if_present(args.build / "licenses" / "OFL-NotoSansKR.txt", runtime / "licenses" / "OFL-NotoSansKR.txt")
        copy_required(args.build / "lisa_date" / "text" / "kor.txt", runtime / "lisa_date" / "text" / "kor.txt")

        # Older stock releases may not carry a provenance sidecar.  Add it only in
        # that case; never replace the original manifest that controls DLSS routes.
        stock_sidecar = runtime / "pt.exe.upscalers.json"
        if not stock_sidecar.exists():
            copy_if_present(args.build / "pt_release.exe.upscalers.json", stock_sidecar)

        missing = [entry for entry in baseline_files if not (runtime / entry).is_file()]
        if missing:
            raise RuntimeError("overlay removed baseline runtime files: " + ", ".join(sorted(missing)))

        allowed_changes = {"pt.exe", "fonts/NotoSansKR.ttf", "licenses/OFL-NotoSansKR.txt", "lisa_date/text/kor.txt"}
        changed = [entry for entry, checksum in baseline_files.items() if digest(runtime / entry) != checksum]
        unexpected = sorted(set(changed) - allowed_changes)
        if unexpected:
            raise RuntimeError("overlay changed original runtime files outside the allowlist: " + ", ".join(unexpected))

        args.out.mkdir(parents=True, exist_ok=True)
        archive_base = args.out / "P.T.PC.Port-windows-x64-Release"
        shutil.make_archive(str(archive_base), "zip", temp, root)


if __name__ == "__main__":
    main()
