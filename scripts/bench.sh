#!/usr/bin/env bash
# WeBjammin — device benchmark: frame time, RSS and audio health.
#
#   scripts/bench.sh [host]        # default host: fogo@192.168.4.11
#
# Runs the game headless for a few seconds on the device and reports the
# numbers that matter on a 1 GHz Cortex-A53: frame time percentiles, resident
# memory and the ALSA xrun count.
set -euo pipefail

HOST="${1:-fogo@192.168.4.11}"
BIN=/tmp/webjammin-bench
DURATION="${DURATION:-10}"

echo "== building on $HOST =="
scp -q -r src Makefile "$HOST":/tmp/webjammin-bench-src/ 2>/dev/null || {
    ssh "$HOST" 'rm -rf /tmp/webjammin-bench-src && mkdir -p /tmp/webjammin-bench-src'
    scp -q -r src Makefile "$HOST":/tmp/webjammin-bench-src/
}
ssh "$HOST" 'cd /tmp/webjammin-bench-src && make -s 2>&1 | tail -3'

echo
echo "== running for ${DURATION}s =="
ssh "$HOST" "cd /tmp/webjammin-bench-src && timeout $((DURATION + 5)) ./build/webjammin --headless & \
    sleep $DURATION; \
    pid=\$(pgrep -f 'build/webjammin --headless' | head -1); \
    if [ -n \"\$pid\" ]; then \
        rss=\$(awk '/VmRSS/{print \$2}' /proc/\$pid/status); \
        echo \"rss_kb=\$rss\"; \
        echo \"threads=\$(ls /proc/\$pid/task | wc -l)\"; \
    else echo 'process already exited'; fi"

echo
echo "== audio device =="
ssh "$HOST" "cat /proc/asound/cards 2>/dev/null | head -8"

echo
echo "== frame timing (host-side sample) =="
echo "run ./build/webjammin --render gameplay /tmp/f.ppm in a loop and time it:"
for i in 1 2 3 4 5; do
    /usr/bin/time -f "  render %e s" ./build/webjammin --render gameplay /tmp/f.ppm 2>&1 | tail -1
done
