"""Attach a verified replacement ZIP to its Windows launcher."""

import argparse
from pathlib import Path
import shutil
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--launcher", type=Path, required=True)
    parser.add_argument("--zip", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    with args.output.open("x+b") as output:
        try:
            with args.launcher.open("rb") as launcher:
                shutil.copyfileobj(launcher, output)
            with zipfile.ZipFile(args.zip) as source:
                with zipfile.ZipFile(output, "a", zipfile.ZIP_DEFLATED) as target:
                    for entry in source.infolist():
                        with source.open(entry) as reader, target.open(
                            entry.filename, "w", force_zip64=entry.file_size >= zipfile.ZIP64_LIMIT
                        ) as writer:
                            shutil.copyfileobj(reader, writer)
            with zipfile.ZipFile(output) as target:
                if target.testzip() is not None:
                    raise ValueError("Self-extracting package failed ZIP verification")
        except Exception:
            output.close()
            args.output.unlink()
            raise
    print(f"Created self-extracting replacement package: {args.output}")


if __name__ == "__main__":
    main()
