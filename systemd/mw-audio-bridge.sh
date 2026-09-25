#!/usr/bin/env bash
# mw-audio-bridge: bridges a raw mono TCP audio stream into an ALSA
# loopback device for miniwolf. Any failure, disconnect, or idle timeout
# exits nonzero/zero so systemd (Restart=always, RestartSec=5) restarts it.

set -euo pipefail

HOST="${MW_AUDIO_HOST:-127.0.0.1}"
PORT="${MW_AUDIO_PORT:-7355}"
IN_RATE="${MW_AUDIO_IN_RATE:-16000}"        # sample rate of the TCP stream
OUT_RATE="${MW_AUDIO_OUT_RATE:-48000}"      # miniwolf capture rate
IDLE_TIMEOUT="${MW_AUDIO_IDLE_TIMEOUT:-30}" # exit after N s without data (nc -w)
RESAMPLER="${MW_AUDIO_RESAMPLER:-auto}"     # auto | sox | ffmpeg
FORMAT="${MW_AUDIO_FORMAT:-f32le}"          # stream sample format: f32le | s16le

if ! aplay -l 2>/dev/null | grep -q 'Loopback'; then
    echo "ALSA loopback device not found; is snd-aloop loaded?" >&2
    exit 1
fi

if [ "$RESAMPLER" = auto ]; then
    if command -v sox >/dev/null 2>&1; then
        RESAMPLER=sox
    elif command -v ffmpeg >/dev/null 2>&1; then
        RESAMPLER=ffmpeg
    else
        echo "no resampler found; install sox or ffmpeg" >&2
        exit 1
    fi
fi

# Both loopback ends must run at OUT_RATE: snd-aloop is a dumb sample pipe
# and does no rate conversion, hence the explicit userspace resampler.
case "$FORMAT" in
f32le) SOX_ENC=(-e floating-point -b 32); FF_IN=(-f f32le) ;;
s16le) SOX_ENC=(-e signed-integer -b 16); FF_IN=(-f s16le) ;;
*)
    echo "unknown MW_AUDIO_FORMAT: $FORMAT (use f32le or s16le)" >&2
    exit 1
    ;;
esac

case "$RESAMPLER" in
sox)
    RESAMPLE_CMD=(sox -q --buffer 2048
        -t raw -r "$IN_RATE" "${SOX_ENC[@]}" -c 1 -
        -t raw -r "$OUT_RATE" -e floating-point -b 32 -c 1 -)
    ;;
ffmpeg)
    RESAMPLE_CMD=(ffmpeg -hide_banner -loglevel error -nostdin
        "${FF_IN[@]}" -ar "$IN_RATE" -ac 1 -i -
        -f f32le -ar "$OUT_RATE" -ac 1 -)
    ;;
*)
    echo "unknown MW_AUDIO_RESAMPLER: $RESAMPLER (use auto, sox or ffmpeg)" >&2
    exit 1
    ;;
esac

nc -w "$IDLE_TIMEOUT" "$HOST" "$PORT" \
    | "${RESAMPLE_CMD[@]}" \
    | aplay -q -t raw -f FLOAT_LE -c 1 -r "$OUT_RATE" \
        --period-size=1024 --buffer-size=32768 \
        -D plughw:Loopback,0,0
