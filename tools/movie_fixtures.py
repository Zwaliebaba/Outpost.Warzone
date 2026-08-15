#!/usr/bin/env python3
"""Build and check the FMV reference fixtures for Phase 6.

MovieTest/RplExtractor.exe drives the shipping ESCAPE 130 decoder and writes raw
RGB565 frames next to a WAV and a manifest. Those dumps are the oracle the
re-encoded movies are judged against -- but they are around 250 MB of raw
frames, and the decoder that produces them is deleted by stage B6.

So the frames are not what gets checked in. Their hashes are. A per-frame
SHA-256 is an exact oracle at 1/2000th the size, it survives the deletion of the
library that produced it, and a converter that gets one frame wrong cannot hide
in an average.

    build  <dumpDir> <fixtureFile>   read the dumps, write the fixture
    check  <dumpDir> <fixtureFile>   compare dumps against an existing fixture

The audio is small enough to keep verbatim, so the WAVs are checked in beside
the fixture and compared byte for byte.
"""

import hashlib
import json
import sys
from pathlib import Path

FIXTURE_VERSION = 1


def frame_hashes(raw_path, frame_bytes, frame_count):
    """SHA-256 of each frame, in order, read without holding the file in memory."""
    hashes = []
    with open(raw_path, "rb") as handle:
        for index in range(frame_count):
            chunk = handle.read(frame_bytes)
            if len(chunk) != frame_bytes:
                raise SystemExit(f"{raw_path}: frame {index} is short ({len(chunk)} of {frame_bytes} bytes)")
            hashes.append(hashlib.sha256(chunk).hexdigest())
        if handle.read(1):
            raise SystemExit(f"{raw_path}: trailing data after {frame_count} frames")
    return hashes


def read_dump(dump_dir, manifest_path):
    meta = json.loads(manifest_path.read_text())
    stem = meta["stem"]

    raw_path = dump_dir / f"{stem}.rgb565"
    if not raw_path.exists():
        raise SystemExit(f"missing {raw_path}")

    entry = {
        "width": meta["width"],
        "height": meta["height"],
        "frameRate": meta["frameRate"],
        "frameCount": meta["framesWritten"],
        "frameBytes": meta["frameBytes"],
        "pixelFormat": meta["pixelFormat"],
        "soundChannels": meta["soundChannels"],
        "soundRate": meta["soundRate"],
        "soundPrecision": meta["soundPrecision"],
        "audioBytes": meta["audioBytes"],
        "frames": frame_hashes(raw_path, meta["frameBytes"], meta["framesWritten"]),
    }

    wav_path = dump_dir / f"{stem}.wav"
    entry["audioSha256"] = hashlib.sha256(wav_path.read_bytes()).hexdigest() if wav_path.exists() else None

    return stem, entry


def collect(dump_dir):
    manifests = sorted(dump_dir.glob("*.json"))
    if not manifests:
        raise SystemExit(f"no dumps in {dump_dir}")
    return dict(read_dump(dump_dir, path) for path in manifests)


def build(dump_dir, fixture_path):
    movies = collect(dump_dir)
    fixture = {"version": FIXTURE_VERSION, "movies": movies}

    fixture_path.parent.mkdir(parents=True, exist_ok=True)
    fixture_path.write_text(json.dumps(fixture, indent=1, sort_keys=True))

    frames = sum(len(entry["frames"]) for entry in movies.values())
    with_audio = sum(1 for entry in movies.values() if entry["audioSha256"])
    print(f"{fixture_path}: {len(movies)} movies, {frames} frames, {with_audio} with an audio track")
    print(f"  {fixture_path.stat().st_size / 1024:.0f} KB")
    return 0


def check(dump_dir, fixture_path):
    fixture = json.loads(fixture_path.read_text())
    if fixture.get("version") != FIXTURE_VERSION:
        raise SystemExit(f"{fixture_path}: fixture version {fixture.get('version')}, expected {FIXTURE_VERSION}")

    expected = fixture["movies"]
    actual = collect(dump_dir)
    failures = 0

    for stem in sorted(set(expected) | set(actual)):
        if stem not in actual:
            print(f"FAIL {stem}: no dump")
            failures += 1
            continue
        if stem not in expected:
            print(f"FAIL {stem}: not in the fixture")
            failures += 1
            continue

        want, got = expected[stem], actual[stem]
        problems = [key for key in want if key not in ("frames", "audioSha256") and want[key] != got.get(key)]
        if problems:
            print(f"FAIL {stem}: {', '.join(f'{k} {want[k]} != {got.get(k)}' for k in problems)}")
            failures += 1
            continue

        differing = [i for i, (a, b) in enumerate(zip(want["frames"], got["frames"])) if a != b]
        if differing:
            shown = ", ".join(str(i) for i in differing[:8])
            more = f" (+{len(differing) - 8} more)" if len(differing) > 8 else ""
            print(f"FAIL {stem}: {len(differing)} frames differ: {shown}{more}")
            failures += 1
            continue

        if want["audioSha256"] != got["audioSha256"]:
            print(f"FAIL {stem}: audio differs")
            failures += 1
            continue

        print(f"ok   {stem}: {len(want['frames'])} frames" + (", audio" if want["audioSha256"] else ""))

    if failures:
        print(f"\n{failures} of {len(expected)} movies failed")
    else:
        print(f"\nall {len(expected)} movies match")
    return 1 if failures else 0


def main(argv):
    if len(argv) != 4 or argv[1] not in ("build", "check"):
        print(__doc__)
        return 2

    command, dump_dir, fixture_path = argv[1], Path(argv[2]), Path(argv[3])
    if not dump_dir.is_dir():
        raise SystemExit(f"no such directory: {dump_dir}")

    return build(dump_dir, fixture_path) if command == "build" else check(dump_dir, fixture_path)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
