"""Prune build/test-only wheel contents and zip known pure-Python modules."""
from pathlib import Path
import argparse
import shutil
import zipfile

# These packages use normal Python imports. Keep DLLs, native modules, and
# packages with filesystem-based resources in the installed site-packages.
ZIP_ROOTS = (
    "anyio", "colorama", "filelock", "flatbuffers", "fsspec", "h11",
    "idna", "packaging", "six.py", "typing_extensions.py",
)
TEST_DIRS = {"tests", "test", "__pycache__"}


def compact(runtime):
    runtime = runtime.resolve(strict=True)
    packages = (runtime / "site-packages").resolve(strict=True)
    if packages.parent != runtime or not (runtime / "python.exe").is_file():
        raise ValueError("Expected a private embedded Python runtime with local site-packages")
    # Keep dist-info licenses and metadata even for packages in the archive.
    for path in sorted(packages.rglob("*"), key=lambda p: len(p.parts), reverse=True):
        relative = path.relative_to(packages)
        if any(part.endswith(".dist-info") for part in relative.parts):
            continue
        if not path.resolve().is_relative_to(packages):
            raise ValueError(f"Wheel path escapes the private runtime: {path}")
        if path.is_file() and path.suffix in {".pyi", ".pyc"}:
            path.unlink()
        elif path.is_dir() and path.name in TEST_DIRS:
            shutil.rmtree(path)

    # OCR uses image operations, not video capture/decoding. This optional
    # OpenCV backend is not a cv2.pyd import and is verified absent by the
    # packaged PDF/OCR suite. Retain all license notices.
    for path in (packages / "cv2").glob("opencv_videoio_ffmpeg*_64.dll"):
        if path.resolve().parent != (packages / "cv2").resolve():
            raise ValueError("Unexpected OpenCV backend location")
        path.unlink()

    archive_path = runtime / "python-dependencies.zip"
    with zipfile.ZipFile(archive_path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name in ZIP_ROOTS:
            root = packages / name
            if not root.exists():
                raise ValueError(f"Missing locked dependency {name}")
            files = sorted(path for path in root.rglob("*") if path.is_file()) if root.is_dir() else [root]
            for path in files:
                if path.suffix.lower() not in {".py", ".typed"}:
                    raise ValueError(f"{path} needs review before ZIP packaging")
                info = zipfile.ZipInfo(path.relative_to(packages).as_posix())
                archive.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED)
            if root.is_dir():
                shutil.rmtree(root)
            else:
                root.unlink()

    (runtime / "python314._pth").write_text(
        "python314.zip\n.\nsite-packages\npython-dependencies.zip\n../document-worker\n",
        encoding="ascii",
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("runtime", type=Path)
    compact(parser.parse_args().runtime.resolve(strict=True))
