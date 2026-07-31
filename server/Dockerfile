# Build context is the repo root (see /railway.json's dockerfilePath),
# so COPY paths below are prefixed with server/.
FROM python:3.11-slim

# yt-dlp needs a JS runtime to solve YouTube's "n challenge" (anti-bot
# signature); without it, only image/storyboard formats are extracted and
# downloads fail with "Requested format is not available". Deno is copied
# in via yt-dlp's own recommended multi-stage pattern (see
# https://github.com/yt-dlp/yt-dlp/wiki/EJS) rather than a curl|sh installer.
COPY --from=denoland/deno:bin-2.9.4 /deno /usr/local/bin/deno

RUN apt-get update \
    && apt-get install -y --no-install-recommends ffmpeg \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY server/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY server/app ./app

ENV PORT=8000
EXPOSE 8000

CMD ["sh", "-c", "uvicorn app.main:app --host 0.0.0.0 --port ${PORT}"]
