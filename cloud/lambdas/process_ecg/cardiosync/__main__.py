"""Analyse a session file pulled from the SD card, without AWS.

python -m cardiosync /path/to/session_1727712000.bin -o out/
"""

import argparse
from pathlib import Path

from .pipeline import analyze


def main() -> None:
    parser = argparse.ArgumentParser(
        prog="cardiosync", description="Analyse a CardioSync session file locally."
    )
    parser.add_argument("file", type=Path, help="session .bin file")
    parser.add_argument("-o", "--out", type=Path, default=None, help="output directory")
    parser.add_argument("--no-plots", action="store_true", help="skip PNG rendering")
    args = parser.parse_args()

    out_dir = args.out or args.file.with_suffix("")
    out_dir.mkdir(parents=True, exist_ok=True)

    metadata, files = analyze(args.file.read_bytes(), source=args.file.name, with_plots=not args.no_plots)
    for name, (content, _) in files.items():
        (out_dir / name).write_bytes(content)

    hr = metadata["heart_rate"]
    print(
        f"{metadata['duration_seconds']:.1f} s | {hr['average_bpm']:.1f} BPM avg | "
        f"lead II: {hr['lead_II']['num_beats']} beats"
    )
    print(f"Wrote {len(files)} files to {out_dir}")


if __name__ == "__main__":
    main()
