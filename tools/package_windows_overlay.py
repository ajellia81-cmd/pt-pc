#!/usr/bin/env python3
"""Overlay this port's executable and localization assets onto a stock Windows release.

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


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


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

        # This is deliberately a tiny overlay.  The official archive supplies the
        # DX12 renderer, DLSS 5/Frame Generation DLLs, shader cache and every
        # upstream data file.  Do not replace those with CI build by-products.
        before = {entry: sha256(runtime / entry) for entry in baseline_files}

        shutil.copy2(exe, runtime / "pt.exe")
        font = args.build / "fonts" / "NotoSansKR.ttf"
        if not font.is_file():
            raise RuntimeError(f"missing Korean font: {font}")
        (runtime / "fonts").mkdir(parents=True, exist_ok=True)
        shutil.copy2(font, runtime / "fonts" / font.name)
        notice = args.build / "licenses" / "OFL-NotoSansKR.txt"
        if notice.is_file():
            (runtime / "licenses").mkdir(parents=True, exist_ok=True)
            shutil.copy2(notice, runtime / "licenses" / notice.name)
        korean_text = args.build / "lisa_date" / "text" / "kor.txt"
        if not korean_text.is_file():
            raise RuntimeError(f"missing Korean Coffee with Lisa text: {korean_text}")
        (runtime / "lisa_date" / "text").mkdir(parents=True, exist_ok=True)
        shutil.copy2(korean_text, runtime / "lisa_date" / "text" / "kor.txt")

        missing = [entry for entry in baseline_files if not (runtime / entry).is_file()]
        if missing:
            raise RuntimeError("overlay removed baseline runtime files: " + ", ".join(sorted(missing)))

        changed = {
            entry for entry in baseline_files
            if sha256(runtime / entry) != before[entry]
        }
        allowed_changed = {"pt.exe"}
        unexpected = changed - allowed_changed
        if unexpected:
            raise RuntimeError("overlay changed original runtime files outside pt.exe: " + ", ".join(sorted(unexpected)))

        args.out.mkdir(parents=True, exist_ok=True)
        archive_base = args.out / "P.T.PC.Port-windows-x64-Release"
        shutil.make_archive(str(archive_base), "zip", temp, root)


if __name__ == "__main__":
    main()
