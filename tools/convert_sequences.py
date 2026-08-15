#!/usr/bin/env python3
"""Convert the game's ESCAPE 130 `.rpl` movies to H.264/AAC in MP4.

Phase 6 stage B2. This runs once, offline, on a developer machine; ffmpeg is a
build-time tool and is never linked into the game or shipped with it. Runtime
playback is Media Foundation.

Two input paths, and which one is used per movie is recorded in the manifest:

  dumps   Encode from MovieTest/RplExtractor's raw RGB565 frames and WAV.
          Preferred, and the default when --dumps is given. Those frames are
          what the shipping decoder produces, verified frame-by-frame against
          MovieTest/Fixtures/reference.json, so the encode starts from exactly
          what the game displays today.

  rpl     Let ffmpeg demux and decode the .rpl itself. The fallback, and the
          only option once stage B6 deletes WINSTR.LIB and the extractor with
          it -- which matters for the 165 movies that live on the CDs. It is
          close but not identical to the shipping decoder: escape130 decodes to
          yuv420p and ffmpeg assumes limited range, so `in_range=full` is
          forced below (without it the error is roughly 11 dB worse), and it
          still yields one spurious trailing frame.

    convert_sequences.py <input>... --out DIR [--dumps DIR] [--crf N]

`<input>` is any mix of `.rpl` files and directories to search. Relative layout
under a directory is preserved in the output, so cam1/cam2/cam3 survive.
"""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

DefaultCrf = 18
DefaultAudioBitrate = "96k"
# x264 spends a lot of time here for a 320x240 clip, and the whole set is under
# two minutes of footage. There is no reason to trade quality for speed.
DefaultPreset = "veryslow"

# The ARMovie header is fixed-order text, one field per line, value first.
HeaderFields = [
    ("magic", 0),
    ("sourcePath", 1),
    ("copyright", 2),
    ("format", 3),
    ("videoFormat", 4),
    ("width", 5),
    ("height", 6),
    ("bitsPerPixel", 7),
    ("frameRate", 8),
    ("soundFormat", 9),
    ("soundRate", 10),
    ("soundChannels", 11),
    ("soundBits", 12),
    ("framesPerChunk", 13),
    ("chunkCount", 14),
]


def find_ffmpeg(explicit):
    """tools/ffmpeg.exe first, then PATH. It is deliberately not in the repo."""
    if explicit:
        if not Path(explicit).exists():
            raise SystemExit(f"no ffmpeg at {explicit}")
        return str(explicit)

    local = Path(__file__).parent / ("ffmpeg.exe" if os.name == "nt" else "ffmpeg")
    if local.exists():
        return str(local)

    found = shutil.which("ffmpeg")
    if found:
        return found

    raise SystemExit(
        "ffmpeg not found.\n"
        "  Put it at tools/ffmpeg.exe (gitignored) or on PATH.\n"
        "  It is a build-time tool for this conversion only; nothing links or ships it."
    )


def read_armovie_header(path):
    """Parse the text header. The chunk count is the frame count, and it is the
    authority: Movie_GetTotalFrames reports one higher, and so does ffmpeg."""
    with open(path, "rb") as handle:
        raw = handle.read(1024)

    lines = raw.split(b"\n")
    if not lines or lines[0].strip() != b"ARMovie":
        raise SystemExit(f"{path}: not an ARMovie file")

    def field(index):
        return lines[index].decode("latin-1", "replace").strip() if index < len(lines) else ""

    def number(index, cast):
        text = field(index).split()
        try:
            return cast(text[0]) if text else cast(0)
        except ValueError:
            return cast(0)

    return {
        "videoFormat": number(4, int),
        "width": number(5, int),
        "height": number(6, int),
        "frameRate": number(8, float),
        "soundFormat": number(9, int),
        "soundRate": number(10, int),
        "soundChannels": number(11, int),
        "soundBits": number(12, int),
        "frameCount": number(14, int),
    }


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


class FfmpegTimeout(Exception):
    """Raised when one file exceeds its budget, so a batch can skip it and go on."""


def run(command, timeout=None):
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        raise FfmpegTimeout(f"exceeded {timeout}s")
    if result.returncode != 0:
        sys.stderr.write(result.stderr[-4000:])
        raise SystemExit(f"ffmpeg failed ({result.returncode})")


def video_args(crf):
    """`-color_range pc` is not decoration. The source is full-range RGB; without
    it the decoder on the other side re-expands limited-range levels and the
    picture comes back washed out."""
    return [
        "-c:v", "libx264",
        "-preset", DefaultPreset,
        "-crf", str(crf),
        "-pix_fmt", "yuv420p",
        "-color_range", "pc",
        "-movflags", "+faststart",
    ]


def audio_args():
    return ["-c:a", "aac", "-b:a", DefaultAudioBitrate]


def encode_from_dump(ffmpeg, dump_dir, stem, header, out_path, crf):
    frames = dump_dir / f"{stem}.rgb565"
    wav = dump_dir / f"{stem}.wav"
    if not frames.exists():
        return None

    meta = json.loads((dump_dir / f"{stem}.json").read_text())
    width, height = meta["width"], meta["height"]
    rate = meta["frameRate"] or header["frameRate"]

    command = [
        ffmpeg, "-hide_banner", "-loglevel", "error", "-y",
        "-f", "rawvideo",
        "-pixel_format", "rgb565le",
        "-video_size", f"{width}x{height}",
        "-framerate", f"{rate}",
        "-i", str(frames),
    ]
    has_audio = wav.exists()
    if has_audio:
        command += ["-i", str(wav)]

    command += video_args(crf)
    command += audio_args() if has_audio else ["-an"]
    command += [str(out_path)]

    run(command)
    return {"source": "dumps", "frames": meta["framesWritten"], "audio": has_audio}


def encode_from_rpl(ffmpeg, rpl_path, header, out_path, crf):
    has_audio = header["soundChannels"] > 0 and header["soundRate"] > 0

    command = [
        ffmpeg, "-hide_banner", "-loglevel", "error", "-y",
        "-i", str(rpl_path),
        # Without this ffmpeg treats escape130's yuv420p as limited range.
        "-vf", "scale=in_range=full:out_range=full",
    ]
    command += video_args(crf)
    command += audio_args() if has_audio else ["-an"]
    command += [str(out_path)]

    run(command)
    return {"source": "rpl", "frames": header["frameCount"], "audio": has_audio}


def measure_video(ffmpeg, dump_dir, stem, meta, out_path):
    """PSNR of the encoded movie against the frames it was made from.

    ffmpeg's own psnr filter rather than a Python loop: 1920 frames of 320x240
    is 147 million pixel comparisons and this is not the place to spend them.
    """
    reference = dump_dir / f"{stem}.rgb565"
    if not reference.exists():
        return None

    command = [
        ffmpeg, "-hide_banner", "-loglevel", "info", "-y",
        "-f", "rawvideo",
        "-pixel_format", "rgb565le",
        "-video_size", f"{meta['width']}x{meta['height']}",
        "-framerate", f"{meta['frameRate']}",
        "-i", str(reference),
        "-i", str(out_path),
        "-lavfi", "[1:v]scale=in_range=full:out_range=full,format=rgb565le[enc];[0:v][enc]psnr",
        "-f", "null", "-",
    ]
    result = subprocess.run(command, capture_output=True, text=True)
    for line in result.stderr.splitlines():
        if "PSNR" in line and "average:" in line:
            for token in line.split():
                if token.startswith("average:"):
                    value = token.split(":", 1)[1]
                    return float("inf") if value == "inf" else float(value)
    return None


def measure_audio(ffmpeg, dump_dir, stem, out_path, scratch):
    """AAC is lossy, so this reports how far the decoded audio moved rather than
    asserting it did not. Sample count is checked exactly; level is not."""
    import struct
    import wave

    reference = dump_dir / f"{stem}.wav"
    if not reference.exists():
        return None

    decoded = scratch / f"{stem}.verify.wav"
    run([ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", str(out_path), "-vn", "-c:a", "pcm_s16le", str(decoded)])

    def samples(path):
        with wave.open(str(path)) as handle:
            raw = handle.readframes(handle.getnframes())
            return struct.unpack("<%dh" % (len(raw) // 2), raw), handle.getframerate()

    want, rate = samples(reference)
    got, _ = samples(decoded)
    decoded.unlink(missing_ok=True)

    count = min(len(want), len(got))
    if count == 0:
        return None

    error = sum((a - b) ** 2 for a, b in zip(want[:count], got[:count])) / count
    signal = sum(a * a for a in want[:count]) / count
    snr = float("inf") if error == 0 else 10 * __import__("math").log10(signal / error) if signal else 0.0
    return {
        "referenceSamples": len(want),
        "decodedSamples": len(got),
        "sampleRate": rate,
        "snrDb": snr,
    }


def gather(inputs):
    found = []
    for item in inputs:
        path = Path(item)
        if path.is_dir():
            found += sorted(p for p in path.rglob("*") if p.suffix.lower() == ".rpl")
        elif path.suffix.lower() == ".rpl":
            found.append(path)
        else:
            raise SystemExit(f"not a .rpl file or directory: {item}")
    if not found:
        raise SystemExit("no .rpl files found")
    return found


def relative_output(rpl_path, inputs, out_dir):
    """Keep any directory structure the input had, so cam1/cam2/cam3 survive."""
    for item in inputs:
        base = Path(item)
        if base.is_dir():
            try:
                return out_dir / rpl_path.relative_to(base).with_suffix(".mp4")
            except ValueError:
                continue
    return out_dir / (rpl_path.stem + ".mp4")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inputs", nargs="+", help=".rpl files or directories to search")
    parser.add_argument("--out", required=True, help="output directory")
    parser.add_argument("--dumps", help="RplExtractor dump directory (preferred input)")
    parser.add_argument("--crf", type=int, default=DefaultCrf)
    parser.add_argument("--manifest", help="where to write the conversion manifest")
    parser.add_argument("--ffmpeg", help="explicit ffmpeg path")
    parser.add_argument("--verify", action="store_true",
                        help="decode each output and measure it against the dumps it came from")
    args = parser.parse_args(argv)

    ffmpeg = find_ffmpeg(args.ffmpeg)
    out_dir = Path(args.out)
    dump_dir = Path(args.dumps) if args.dumps else None

    movies = gather(args.inputs)
    entries = []
    fallbacks = []
    total_in = total_out = 0

    for rpl_path in movies:
        header = read_armovie_header(rpl_path)
        out_path = relative_output(rpl_path, args.inputs, out_dir)
        out_path.parent.mkdir(parents=True, exist_ok=True)

        result = None
        if dump_dir is not None:
            result = encode_from_dump(ffmpeg, dump_dir, rpl_path.stem, header, out_path, args.crf)
        if result is None:
            result = encode_from_rpl(ffmpeg, rpl_path, header, out_path, args.crf)
            fallbacks.append(rpl_path.name)

        in_bytes = rpl_path.stat().st_size
        out_bytes = out_path.stat().st_size
        total_in += in_bytes
        total_out += out_bytes

        entries.append({
            "source": str(rpl_path).replace("\\", "/"),
            "sourceSha256": sha256_of(rpl_path),
            "sourceBytes": in_bytes,
            "output": str(out_path.relative_to(out_dir)).replace("\\", "/"),
            "outputSha256": sha256_of(out_path),
            "outputBytes": out_bytes,
            "width": header["width"],
            "height": header["height"],
            "frameRate": header["frameRate"],
            "frameCount": result["frames"],
            "headerChunkCount": header["frameCount"],
            "audio": result["audio"],
            "encodedFrom": result["source"],
        })

        quality = ""
        if args.verify and dump_dir is not None and result["source"] == "dumps":
            meta = json.loads((dump_dir / f"{rpl_path.stem}.json").read_text())
            psnr = measure_video(ffmpeg, dump_dir, rpl_path.stem, meta, out_path)
            if psnr is not None:
                entries[-1]["psnrDb"] = psnr
                quality = f"  {psnr:5.2f} dB"
            audio = measure_audio(ffmpeg, dump_dir, rpl_path.stem, out_path, out_dir)
            if audio is not None:
                entries[-1]["audioSnr"] = audio
                drift = audio["decodedSamples"] - audio["referenceSamples"]
                quality += f"  a:{audio['snrDb']:5.1f} dB {drift:+d} smp"

        ratio = in_bytes / out_bytes if out_bytes else 0
        print(f"{rpl_path.stem:16s} {header['width']:>4d}x{header['height']:<4d} "
              f"{in_bytes // 1024:>5d} KB -> {out_bytes // 1024:>5d} KB  ({ratio:4.1f}x)  "
              f"{'audio' if result['audio'] else '     '}  via {result['source']}{quality}")

    manifest_path = Path(args.manifest) if args.manifest else out_dir / "sequences.json"
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps({
        "version": 1,
        "crf": args.crf,
        "preset": DefaultPreset,
        "audioBitrate": DefaultAudioBitrate,
        "movies": entries,
    }, indent=1, sort_keys=True))

    print(f"\n{len(entries)} movies: {total_in // 1024} KB -> {total_out // 1024} KB "
          f"({total_in / total_out:.1f}x smaller)")
    print(f"manifest: {manifest_path}")

    # Never let the fallback path be a silent default: it is measurably not the
    # shipping decoder's output, and which movies took it has to be visible.
    if fallbacks:
        print(f"\nWARNING: {len(fallbacks)} movies were decoded by ffmpeg rather than from extractor dumps.")
        print("  These are close to, but not identical to, what the game decodes today.")
        for name in fallbacks[:10]:
            print(f"    {name}")
        if len(fallbacks) > 10:
            print(f"    (+{len(fallbacks) - 10} more)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
