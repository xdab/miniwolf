#!/usr/bin/env bash
# mw-audio-bridge: bridges a raw mono f32le TCP audio stream into an ALSA
# loopback device for miniwolf. Any failure, disconnect, or idle timeout
# exits nonzero/zero so systemd (Restart=always, RestartSec=5) restarts it.

set -euo pipefail

HOST="${MW_AUDIO_HOST:-127.0.0.1}"
PORT="${MW_AUDIO_PORT:-7355}"
IN_RATE="${MW_AUDIO_IN_RATE:-16000}"        # sample rate of the TCP stream
OUT_RATE="${MW_AUDIO_OUT_RATE:-48000}"      # miniwolf capture rate
IDLE_TIMEOUT="${MW_AUDIO_IDLE_TIMEOUT:-30}" # exit after N s without data (nc -w)

if ! aplay -l 2>/dev/null | grep -q 'Loopback'; then
    echo "ALSA loopback device not found; is snd-aloop loaded?" >&2
    exit 1
fi

# Both loopback ends must run at OUT_RATE: snd-aloop is a dumb sample pipe
# and does no rate conversion, hence the explicit resampler (ffmpeg).
nc -w "$IDLE_TIMEOUT" "$HOST" "$PORT" \
    | ffmpeg -hide_banner -loglevel error -nostdin \
        -f f32le -ar "$IN_RATE" -ac 1 -i - \
        -f f32le -ar "$OUT_RATE" -ac 1 - \
    | aplay -q -t raw -f FLOAT_LE -c 1 -r "$OUT_RATE" \
        --period-size=1024 --buffer-size=4096 \
        -D plughw:Loopback,0,0
