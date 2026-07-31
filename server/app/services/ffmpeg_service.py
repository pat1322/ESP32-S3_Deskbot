import asyncio
import re


class FfmpegError(RuntimeError):
    pass


_DURATION_RE = re.compile(r"Duration:\s*(\d+):(\d+):(\d+\.\d+)")


async def _run(*args: str) -> None:
    proc = await asyncio.create_subprocess_exec(
        *args,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    try:
        _, stderr = await proc.communicate()
    except asyncio.CancelledError:
        # don't leave an orphaned ffmpeg process burning CPU after a cancel
        proc.kill()
        await proc.wait()
        raise
    if proc.returncode != 0:
        raise FfmpegError(stderr.decode(errors="replace")[-2000:])


async def extract_audio(source_path: str, mp3_path: str) -> None:
    await _run(
        "ffmpeg", "-y",
        "-i", source_path,
        "-vn",
        "-ac", "1",
        "-ar", "44100",
        "-b:a", "96k",
        mp3_path,
    )


async def probe_duration(source_path: str) -> float:
    """Duration in seconds, for uploaded files that don't come with yt-dlp
    metadata. Parses ffmpeg's own stderr banner rather than shelling out to
    a separate ffprobe binary — `ffmpeg -i <file>` with no output always
    exits non-zero, but prints `Duration: HH:MM:SS.ms` to stderr before it
    does, so we read that instead of treating the exit code as success.
    """
    proc = await asyncio.create_subprocess_exec(
        "ffmpeg", "-i", source_path,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    _, stderr = await proc.communicate()
    text = stderr.decode(errors="replace")
    match = _DURATION_RE.search(text)
    if not match:
        raise FfmpegError(f"could not determine duration: {text[-500:]}")
    hours, minutes, seconds = match.groups()
    return int(hours) * 3600 + int(minutes) * 60 + float(seconds)


# Deterministic, single-pass ffmpeg color grades for the "bw"/"cinematic"
# photo filter choices (routers/upload.py) -- no model, no extra
# dependency, unlike the "anime" choice (services/anime_service.py).
# "cinematic" pushes shadows toward teal and highlights toward orange
# (colorbalance) with a mild contrast/saturation lift (eq) -- a standard,
# well-known grade, not anything ML-derived.
PHOTO_FILTERS = {
    "bw": "format=gray",
    "cinematic": (
        "eq=contrast=1.25:saturation=1.35,"
        "colorbalance=rs=-0.18:gs=0.08:bs=0.25:rm=0.1:gm=0:bm=-0.1:rh=0.25:gh=0.08:bh=-0.25"
    ),
}


async def extract_still(
    source_path: str,
    jpeg_path: str,
    quality: int = 5,
    width: int = 320,
    height: int = 240,
    vf_extra: str | None = None,
) -> None:
    # Same crop-to-fill scaling as extract_mjpeg, one frame instead of a
    # stream. Lower default quality number (better) than extract_mjpeg's --
    # a single still's file size doesn't need to stay small for
    # streaming/decode-speed reasons the way a continuous fps stream does.
    #
    # Scale/crop down to the target size FIRST, then apply vf_extra (the
    # "bw"/"cinematic" color grades) on the already-small image -- not the
    # other way around. A source photo can be a full-resolution phone
    # upload (up to MAX_UPLOAD_PHOTO_MB, no pixel-dimension cap), and
    # running a per-pixel filter chain (eq/colorbalance) against that
    # before downscaling is the exact same class of problem the anime
    # filter had (see services/anime_service.py's resolution cap, added
    # to fix a Railway OOM on large uploads) -- wasted work at best,
    # a resource-constrained-container failure at worst, for a result
    # that looks identical either way since these are pointwise/local
    # grades that commute with resizing.
    vf = f"scale={width}:{height}:force_original_aspect_ratio=increase,crop={width}:{height}"
    if vf_extra:
        vf = f"{vf},{vf_extra}"
    await _run(
        "ffmpeg", "-y",
        "-i", source_path,
        "-vframes", "1",
        "-vf", vf,
        "-q:v", str(quality),
        jpeg_path,
    )


async def extract_mjpeg(
    source_path: str,
    mjpeg_path: str,
    fps: int,
    quality: int = 10,
    width: int = 320,
    height: int = 240,
) -> None:
    # quality is ffmpeg's mjpeg -q:v scale: 2 (best/largest) .. 31 (worst/smallest).
    vf = f"fps={fps},scale={width}:{height}:force_original_aspect_ratio=increase,crop={width}:{height}"
    await _run(
        "ffmpeg", "-y",
        "-i", source_path,
        "-vf", vf,
        "-q:v", str(quality),
        "-f", "mjpeg",
        "-an",
        mjpeg_path,
    )
