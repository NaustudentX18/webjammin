#!/usr/bin/env bash
# WeBjammin — dependency setup for Debian arm64 (Cardputer Zero).
#
#   sudo scripts/setup-debian.sh
#
# The game needs very little: a C++20 compiler, make and the ALSA headers.
# curl and unzip are used for catalogue downloads and are usually present.
set -euo pipefail

APT_PACKAGES=(build-essential libasound2-dev curl unzip)

echo "Installing: ${APT_PACKAGES[*]}"
apt-get update
apt-get install -y --no-install-recommends "${APT_PACKAGES[@]}"

echo
echo "Checking the essentials:"
for tool in g++ make curl unzip; do
    if command -v "$tool" >/dev/null 2>&1; then
        printf '  %-8s %s\n' "$tool" "$(command -v "$tool")"
    else
        printf '  %-8s MISSING\n' "$tool"
    fi
done

echo
echo "Sound cards on this device:"
aplay -l 2>/dev/null || echo "  (aplay not available)"

cat <<'EOF'

Next steps:
  1. mkdir -p ~/.config/webjammin && cp config/config.json ~/.config/webjammin/
  2. make && make test
  3. ./build/webjammin --generate-songs ~/webjammin/songs
  4. ./build/webjammin

The audio device in the config must be the ES8389 (card 1):
  plughw:CARD=ES8389Audio,DEV=0
EOF
