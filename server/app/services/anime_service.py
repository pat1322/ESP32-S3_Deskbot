import asyncio
import os

import numpy as np
import onnxruntime as ort
from PIL import Image

_MODEL_PATH = os.path.join(os.path.dirname(__file__), "..", "data", "face_paint_512_v2.onnx")

# Loaded once and reused — session construction (reading/parsing the ~9MB
# model) is the expensive part; actual inference calls on an existing
# session are cheap to repeat. Lazy rather than at import time so a
# request that never touches the anime filter never pays for it.
_session: ort.InferenceSession | None = None


def _get_session() -> ort.InferenceSession:
    global _session
    if _session is None:
        _session = ort.InferenceSession(_MODEL_PATH, providers=["CPUExecutionProvider"])
    return _session


def _round32(x: int) -> int:
    # Model is fully convolutional (bryandlee/animegan2-pytorch's
    # Generator uses F.interpolate against the live input size, not a
    # fixed shape) but still expects dimensions that are multiples of
    # 32 — same resize rule the reference repo's own inference script
    # uses, not something we're free to change.
    return 256 if x < 256 else x - x % 32


def _stylize_sync(source_path: str, dest_path: str) -> None:
    session = _get_session()
    img = Image.open(source_path).convert("RGB")

    w, h = img.size
    new_w, new_h = _round32(w), _round32(h)
    img = img.resize((new_w, new_h), Image.BICUBIC)

    # Preprocessing matches the reference repo's test.py exactly:
    # to_tensor(image).unsqueeze(0) * 2 - 1 (HWC uint8 [0,255] -> NCHW
    # float32 [-1, 1]). Verified against the actual reference source
    # during implementation, not guessed.
    arr = np.asarray(img).astype(np.float32) / 255.0
    arr = arr.transpose(2, 0, 1)[None, ...]
    arr = arr * 2 - 1

    input_name = session.get_inputs()[0].name
    out = session.run(None, {input_name: arr})[0]

    # Postprocessing, also matching the reference repo:
    # out.squeeze(0).clip(-1, 1) * 0.5 + 0.5 -> HWC uint8 [0, 255].
    out = np.clip(out[0], -1, 1) * 0.5 + 0.5
    out = (out.transpose(1, 2, 0) * 255).astype(np.uint8)
    Image.fromarray(out).save(dest_path, quality=92)


async def stylize(source_path: str, dest_path: str) -> None:
    # Inference is CPU-bound and blocking (~1s for a typical webcam-sized
    # photo on real hardware, verified locally before shipping this) —
    # same asyncio.to_thread() pattern ytdlp_service.py already uses for
    # its own blocking calls, so it doesn't stall the event loop.
    await asyncio.to_thread(_stylize_sync, source_path, dest_path)
