#!/usr/bin/env python3
"""Convert Theora/Vorbis `.ogg` movies to the same H.264/AAC MP4 the `.rpl`
conversion produces.

Phase 6 stage B2, second source. 164 of the 181 movies the game asks for were
never in this repository -- they shipped on the CDs. An OGG set covers 155 of
them, and this converts that set into the same target format, with the same
encoder settings, as `convert_sequences.py`. The settings are imported from that
script rather than repeated, so the two sources cannot drift into looking
different from each other.

What this does to the source, and why -- all four were measured, not assumed:

  Range      The OGG files are limited-range yuv420p. Against the reference
             decode of the same movies, expanding limited->full scores ~35 dB
             and treating them as full-range scores ~25 dB. So they are
             expanded, which also matches the full-range output of the .rpl
             path.

  Frame rate 57 of the 178 files are 12.5 fps against the originals' 25. The
             duration is right, so the information is simply gone and nothing
             here can bring it back. Output is normalised to 25 fps anyway:
             subtitle cues in sequenceAudio/*.txt are authored as frame numbers
             at 25 fps, and a single rate across every movie means
             seq_GetFrameTimeInClicks keeps returning 40 for all of them.
             Duplicated frames cost almost nothing to encode.

  Audio      Vorbis, 22050 Hz, stereo -- a pure upmix of the mono originals,
             L == R for 100% of samples with a maximum difference of 0. It is
             passed through as stereo anyway. Downmixing looks obviously right
             and is not: `-ac 1` deadlocks ffmpeg on the badly interleaved files
             described below, and it saves 272 bytes on a 892 KB movie because
             AAC joint stereo already encodes a duplicated channel for almost
             nothing. A micro-optimisation that hangs the tool is a bad trade.

  Silence    11 files carry no audio track, and they are exactly the movies the
             game pairs with a sequenceAudio/*.wav. That split is preserved --
             muxing audio into them would break the loop-and-hold timing that
             SeqDisp.cpp drives from the external wav.

    convert_ogg.py <input>... --out DIR [--check] [--crf N]
"""

import argparse
import json
import os
import re
import subprocess
import tempfile
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from convert_sequences import (DefaultCrf, DefaultPreset, DefaultAudioBitrate, FfmpegTimeout, find_ffmpeg, run, sha256_of,
                               video_args)

# The originals are 25 fps and the subtitle cues are frame numbers against it.
TargetFps = 25

# The longest of these is 85 seconds and veryslow runs well ahead of realtime,
# so a couple of minutes is generous. The point is that a file which stalls
# costs one entry in a report rather than the rest of the batch.
EncodeTimeoutSeconds = 180

# Sampled at this rate when --check looks for the green-block corruption.
CheckFps = 5
# A frame with more than this fraction of hard green is reported for review.
GreenFrameThreshold = 0.10


def probe(ffmpeg, path):
    """Duration, geometry and stream presence. ffprobe is not assumed to exist."""
    err = subprocess.run([ffmpeg, "-hide_banner", "-i", str(path)], capture_output=True, text=True).stderr

    duration = re.search(r"Duration: (\d+):(\d+):(\d+\.\d+)", err)
    seconds = 0.0
    if duration:
        seconds = int(duration.group(1)) * 3600 + int(duration.group(2)) * 60 + float(duration.group(3))

    video_line = next((line for line in err.splitlines() if "Video:" in line), "")
    audio_line = next((line for line in err.splitlines() if "Audio:" in line), "")

    size = re.search(r", (\d+)x(\d+)", video_line)
    # Some streams report "25 fps", others only "25 tbr".
    rate = re.search(r"([\d.]+) fps", video_line) or re.search(r"([\d.]+) tbr", video_line)
    sample_rate = re.search(r"(\d+) Hz", audio_line)

    return {
        "seconds": round(seconds, 3),
        "width": int(size.group(1)) if size else 0,
        "height": int(size.group(2)) if size else 0,
        "fps": float(rate.group(1)) if rate else 0.0,
        "hasVideo": bool(video_line),
        "hasAudio": bool(audio_line),
        "sampleRate": int(sample_rate.group(1)) if sample_rate else 0,
    }


def green_corruption(ffmpeg, path, width, height):
    """Fraction of the worst frame that is hard green.

    Some of these files have rectangular blocks where the chroma planes are
    zeroed, which decodes to a flat green rectangle over the picture. This
    reports rather than decides: legitimately green footage (explosions, the
    NEXUS effects) scores a few percent, and the corrupt files score far higher,
    but the boundary is a judgement no threshold makes for you.
    """
    scale_w, scale_h = max(width // 2, 1), max(height // 2, 1)
    pixels = scale_w * scale_h
    if pixels == 0:
        return 0.0, 0

    # Raw frames go to a temp file rather than down a pipe. Reading megabytes of
    # binary video from ffmpeg's stdout on Windows returns the right *number* of
    # bytes but occasionally the wrong bytes: six identical runs over one clean
    # movie scored 0, 0, 93, 0, 93, 0 percent, every one of them reading exactly
    # 5,299,200 bytes. Through a file the same six runs are all 0. That silent
    # corruption would have condemned good movies as broken.
    with tempfile.TemporaryDirectory() as scratch:
        raw = Path(scratch) / "frames.raw"
        command = [
            ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
            # See DecodeThreads.
            "-threads", "1", "-i", str(path),
            "-vf", f"fps={CheckFps},scale={scale_w}:{scale_h},format=rgb24",
            "-f", "rawvideo", str(raw),
        ]
        subprocess.run(command, capture_output=True)
        if not raw.exists():
            return 0.0, 0
        data = raw.read_bytes()

    if not data:
        return 0.0, 0

    # Per-pixel work in Python is far too slow for 178 files, so each channel is
    # thresholded with bytes.translate and the three results are combined as one
    # big-integer AND. A matching pixel contributes 0xFF to all three, so the
    # population count of the result divided by eight is the pixel count.
    high = bytes.maketrans(bytes(range(256)), bytes(0xFF if v > 160 else 0 for v in range(256)))
    low = bytes.maketrans(bytes(range(256)), bytes(0xFF if v < 48 else 0 for v in range(256)))

    worst = 0.0
    frames = len(data) // (pixels * 3)
    for frame in range(frames):
        chunk = data[frame * pixels * 3:(frame + 1) * pixels * 3]
        green = int.from_bytes(chunk[1::3].translate(high), "big")
        red = int.from_bytes(chunk[0::3].translate(low), "big")
        blue = int.from_bytes(chunk[2::3].translate(low), "big")
        worst = max(worst, ((green & red & blue).bit_count() // 8) / pixels)
    return worst, frames


def encode(ffmpeg, source, info, out_path, crf):
    filters = [
        # Measured: without this the picture is ~10 dB darker in the shadows.
        "scale=in_range=limited:out_range=full",
        f"fps={TargetFps}",
    ]

    command = [
        ffmpeg, "-hide_banner", "-loglevel", "error",
        # ffmpeg reads stdin for interactive keys. Nothing here types at it, and
        # a batch that inherits a shared stdin can block on it.
        "-nostdin", "-y",
        # Decoding single-threaded, deliberately. Several of these streams are
        # damaged, and ffmpeg's multithreaded Theora decoder conceals errors
        # differently depending on thread scheduling: three decodes of
        # cam3/c3ad2n2.ogg produced three different frame hashes, and of
        # cam1/cam1dnp.ogg two. That made both the corruption check and the
        # encoded output vary run to run. With -threads 1 every file hashes
        # identically every time. Healthy files were already deterministic, so
        # this costs only decode speed on a job that runs once.
        "-threads", "1",
        "-i", str(source),
        # The .ogg carries a bare data stream alongside the media; take only
        # what is wanted rather than letting the muxer guess.
        "-map", "0:v:0",
    ]
    if info["hasAudio"]:
        command += ["-map", "0:a:0"]

    command += ["-vf", ",".join(filters)]
    command += video_args(crf)

    if info["hasAudio"]:
        # Channels are passed through untouched. Both `-ac 1` and an equivalent
        # `pan=mono` filter deadlock on the badly interleaved sources below --
        # video stops at frame 32 of 330 and ffmpeg waits forever rather than
        # failing -- and the downmix they would buy is worth 272 bytes.
        command += ["-c:a", "aac", "-b:a", DefaultAudioBitrate]
        # Roughly eighteen of these files are interleaved badly enough that the
        # muxer's default policy stalls: it holds every packet until it has one
        # from each stream, that packet never arrives, and the queue backs up
        # into the encoder. Disabling the interleave buffer clears it, and
        # unlike +genpts it changes only the write strategy, not the timestamps.
        command += ["-max_interleave_delta", "0"]
    else:
        command += ["-an"]

    command += [str(out_path)]
    run(command, timeout=EncodeTimeoutSeconds)


def gather(inputs):
    found = []
    for item in inputs:
        path = Path(item)
        if path.is_dir():
            found += sorted(p for p in path.rglob("*") if p.suffix.lower() == ".ogg")
        elif path.suffix.lower() == ".ogg":
            found.append(path)
        else:
            raise SystemExit(f"not an .ogg file or directory: {item}")
    if not found:
        raise SystemExit("no .ogg files found")
    return found


def relative_output(source, inputs, out_dir):
    """cam1/cam2/cam3 have to survive: the game asks for them by that path."""
    for item in inputs:
        base = Path(item)
        if base.is_dir():
            try:
                return out_dir / source.relative_to(base).with_suffix(".mp4")
            except ValueError:
                continue
    return out_dir / (source.stem + ".mp4")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inputs", nargs="+", help=".ogg files or directories to search")
    parser.add_argument("--out", required=True, help="output directory")
    parser.add_argument("--crf", type=int, default=DefaultCrf)
    parser.add_argument("--manifest", help="where to write the conversion manifest")
    parser.add_argument("--ffmpeg", help="explicit ffmpeg path")
    parser.add_argument("--check", action="store_true",
                        help="also scan each source for the green-block chroma corruption")
    parser.add_argument("--skip-existing", action="store_true",
                        help="leave any output that is already present. Run the .rpl conversion first and this "
                             "makes the original assets win every conflict, which is what you want: they are the "
                             "shipping pixels, and the OGG set is a lossy third-party re-encode of the same source.")
    args = parser.parse_args(argv)

    ffmpeg = find_ffmpeg(args.ffmpeg)
    out_dir = Path(args.out)
    sources = gather(args.inputs)

    entries = []
    resampled = []
    suspect = []
    skipped = []
    stalled = []
    total_in = total_out = 0

    for source in sources:
        info = probe(ffmpeg, source)
        if not info["hasVideo"]:
            print(f"SKIP {source.name}: no video stream")
            continue

        out_path = relative_output(source, args.inputs, out_dir)
        if args.skip_existing and out_path.exists():
            skipped.append(str(out_path.relative_to(out_dir)))
            continue

        out_path.parent.mkdir(parents=True, exist_ok=True)
        try:
            encode(ffmpeg, source, info, out_path, args.crf)
        except FfmpegTimeout as expired:
            # Leave nothing half-written to be mistaken for a converted movie.
            out_path.unlink(missing_ok=True)
            stalled.append((str(source), str(expired)))
            print(f"STALLED {source.name}: {expired}")
            continue

        in_bytes = source.stat().st_size
        out_bytes = out_path.stat().st_size
        total_in += in_bytes
        total_out += out_bytes

        entry = {
            "source": str(source).replace("\\", "/"),
            "sourceSha256": sha256_of(source),
            "sourceBytes": in_bytes,
            "output": str(out_path.relative_to(out_dir)).replace("\\", "/"),
            "outputSha256": sha256_of(out_path),
            "outputBytes": out_bytes,
            "width": info["width"],
            "height": info["height"],
            "sourceFps": info["fps"],
            "outputFps": TargetFps,
            "seconds": info["seconds"],
            "audio": info["hasAudio"],
            "encodedFrom": "ogg",
        }

        note = ""
        if info["fps"] and abs(info["fps"] - TargetFps) > 0.01:
            resampled.append((source.name, info["fps"]))
            note += f"  {info['fps']:g}->{TargetFps} fps"

        if args.check:
            worst, frames = green_corruption(ffmpeg, source, info["width"], info["height"])
            entry["greenWorstFrame"] = round(worst, 4)
            if worst >= GreenFrameThreshold:
                suspect.append((str(out_path.relative_to(out_dir)), worst))
                note += f"  GREEN {worst * 100:.0f}%"

        entries.append(entry)
        ratio = in_bytes / out_bytes if out_bytes else 0
        print(f"{str(out_path.relative_to(out_dir)):<26s} {info['width']:>4d}x{info['height']:<4d} "
              f"{in_bytes // 1024:>6d} KB -> {out_bytes // 1024:>6d} KB ({ratio:4.1f}x) "
              f"{'audio' if info['hasAudio'] else '     '}{note}")

    manifest_path = Path(args.manifest) if args.manifest else out_dir / "sequences-ogg.json"
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps({
        "version": 1,
        "crf": args.crf,
        "preset": DefaultPreset,
        "audioBitrate": DefaultAudioBitrate,
        "audioChannels": "source",
        "targetFps": TargetFps,
        "movies": entries,
    }, indent=1, sort_keys=True))

    print(f"\n{len(entries)} movies: {total_in // 1024} KB -> {total_out // 1024} KB")
    print(f"manifest: {manifest_path}")

    if skipped:
        print(f"\n{len(skipped)} already present, left alone (the .rpl conversion wins these):")
        for name in skipped[:6]:
            print(f"    {name}")
        if len(skipped) > 6:
            print(f"    (+{len(skipped) - 6} more)")

    # Both of these are quality losses the conversion cannot undo, so neither is
    # allowed to pass silently.
    if resampled:
        print(f"\nNOTE: {len(resampled)} sources were below {TargetFps} fps and have been frame-duplicated.")
        print("  The duration is right, but the missing frames are missing in the source.")
        for name, fps in resampled[:10]:
            print(f"    {name} ({fps:g} fps)")
        if len(resampled) > 10:
            print(f"    (+{len(resampled) - 10} more)")

    if suspect:
        print(f"\nWARNING: {len(suspect)} sources show green-block chroma corruption. Review these by eye.")
        for name, worst in sorted(suspect, key=lambda x: -x[1]):
            print(f"    {name:<28s} worst frame {worst * 100:.0f}% green")

    return 0


if __name__ == "__main__":
    sys.exit(main())
