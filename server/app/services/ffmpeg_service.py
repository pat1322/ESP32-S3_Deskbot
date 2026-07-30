import asyncio


class FfmpegError(RuntimeError):
    pass


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
