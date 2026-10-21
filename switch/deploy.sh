#!/bin/sh
# Copies the Switch build to the console over FTP and fetches the log back.
#   switch/deploy.sh <console IP> [push|logs]   (default: push)
# push: out/build/switch/cvhd.nro -> sdmc:/switch/cvhd/cvhd.nro. The game data goes once, by hand:
#       game/ -> sdmc:/switch/cvhd/game/ (default.xex and data/; never default.xexp for now).
# logs: cvhd_log.txt -> switch/logs/ (git-ignored).
set -e
cd "$(dirname "$0")/.."
IP=$1
ACTION=${2:-push}
[ -n "$IP" ] || { sed -n '2,7p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }
URL="ftp://$IP:${SWITCH_FTP_PORT:-5000}/switch/cvhd"
case "$ACTION" in
    push)
        curl -sS --ftp-create-dirs -T "${SWITCH_BUILD_DIR:-out/build/switch}/cvhd.nro" "$URL/cvhd.nro"
        echo "Copied cvhd.nro to sdmc:/switch/cvhd/ on $IP."
        ;;
    logs)
        mkdir -p switch/logs
        curl -sS -o switch/logs/cvhd_log.txt "$URL/cvhd_log.txt" && echo switch/logs/cvhd_log.txt
        ;;
    *)
        echo "Unknown action: $ACTION (push or logs)" >&2
        exit 1
        ;;
esac
