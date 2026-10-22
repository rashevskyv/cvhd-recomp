#!/bin/sh
# Copies the Switch build to the console over FTP, verifies it by SHA-1, and fetches the logs back.
#   switch/deploy.sh <console IP> [push|logs]   (default: push)
# push: out/build/switch/cvhd.nro -> sdmc:/switch/cvhd/cvhd.nro, then downloads it again and compares
#       the SHA-1 with the local file (the console has gone offline mid-upload before). The game data
#       goes once, by hand: game/ -> sdmc:/switch/cvhd/game/ (default.xex and data/; no default.xexp).
# logs: cvhd_log.txt and cvhd_rex_log.txt -> switch/logs/ (git-ignored).
set -e
cd "$(dirname "$0")/.."
IP=$1
ACTION=${2:-push}
[ -n "$IP" ] || { sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
URL="ftp://$IP:${SWITCH_FTP_PORT:-5000}/switch/cvhd"
NRO=${SWITCH_BUILD_DIR:-out/build/switch}/cvhd.nro
case "$ACTION" in
    push)
        # The NACP (what hbmenu shows) must carry this build's version before anything goes out.
        python3 tools/nro_nacp.py "$NRO" "$(grep -o 'CVHD_VERSION "[^"]*"' out/build/switch/cvhd_version.h | cut -d'"' -f2)"
        curl -sS --ftp-create-dirs -T "$NRO" "$URL/cvhd.nro"
        local_sha=$(sha1sum "$NRO" | cut -c1-40)
        remote_sha=$(curl -sS "$URL/cvhd.nro" | sha1sum | cut -c1-40)
        if [ "$local_sha" = "$remote_sha" ]; then
            echo "Copied and verified cvhd.nro ($local_sha) on $IP, $(grep -o 'CVHD_VERSION "[^"]*"' out/build/switch/cvhd_version.h)."
        else
            echo "VERIFY FAILED: local $local_sha, console $remote_sha" >&2
            exit 1
        fi
        ;;
    logs)
        mkdir -p switch/logs
        for log in cvhd_log.txt cvhd_rex_log.txt; do
            curl -sS -o "switch/logs/$log" "$URL/$log" && echo "switch/logs/$log" || true
        done
        ;;
    *)
        echo "Unknown action: $ACTION (push or logs)" >&2
        exit 1
        ;;
esac
