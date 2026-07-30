import asyncio
from dataclasses import dataclass

import yt_dlp

from ..config import settings


@dataclass
class DownloadResult:
    source_path: str
    title: str
    duration: int | None


def _search_sync(query: str, limit: int) -> list[dict]:
    opts = {
        "quiet": True,
        "no_warnings": True,
        "extract_flat": True,
        "skip_download": True,
        "default_search": "ytsearch",
    }
    with yt_dlp.YoutubeDL(opts) as ydl:
        info = ydl.extract_info(f"ytsearch{limit}:{query}", download=False)

    entries = info.get("entries", []) if info else []
    results = []
    for e in entries:
        if not e:
            continue
        results.append(
            {
                "video_id": e.get("id"),
                "title": e.get("title") or "Untitled",
                "duration": e.get("duration"),
                "thumbnail_url": e.get("thumbnail") or _thumb_url(e.get("id")),
                "channel": e.get("channel") or e.get("uploader"),
            }
        )
    return results


def _thumb_url(video_id: str | None) -> str | None:
    if not video_id:
        return None
    return f"https://i.ytimg.com/vi/{video_id}/mqdefault.jpg"


async def search(query: str, limit: int | None = None) -> list[dict]:
    limit = limit or settings.search_result_limit
    return await asyncio.to_thread(_search_sync, query, limit)


def _download_sync(video_id: str, out_dir: str) -> DownloadResult:
    opts = {
        "quiet": True,
        "no_warnings": True,
        "format": "bv*[height<=480]+ba/b[height<=480]",
        "outtmpl": f"{out_dir}/source.%(ext)s",
        "merge_output_format": "mp4",
        "noplaylist": True,
    }
    with yt_dlp.YoutubeDL(opts) as ydl:
        info = ydl.extract_info(f"https://www.youtube.com/watch?v={video_id}", download=True)
        path = ydl.prepare_filename(info)
        if not path.endswith(".mp4"):
            # merge_output_format should force mp4, but fall back defensively
            import os

            base, _ = os.path.splitext(path)
            candidate = base + ".mp4"
            if os.path.exists(candidate):
                path = candidate

    return DownloadResult(source_path=path, title=info.get("title") or video_id, duration=info.get("duration"))


async def get_metadata(video_id: str) -> dict:
    def _meta():
        opts = {"quiet": True, "no_warnings": True, "skip_download": True}
        with yt_dlp.YoutubeDL(opts) as ydl:
            return ydl.extract_info(f"https://www.youtube.com/watch?v={video_id}", download=False)

    return await asyncio.to_thread(_meta)


async def download(video_id: str, out_dir: str) -> DownloadResult:
    return await asyncio.to_thread(_download_sync, video_id, out_dir)
