#!/bin/sh
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"

export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
if [ -z "$WAYLAND_DISPLAY" ]; then
	for sock in "$XDG_RUNTIME_DIR"/wayland-*; do
		case "$sock" in *.lock) continue ;; esac
		[ -S "$sock" ] && WAYLAND_DISPLAY="$(basename "$sock")" && break
	done
fi
if [ -z "$WAYLAND_DISPLAY" ]; then
	echo "Wayland socket not found in $XDG_RUNTIME_DIR — is sxmo-de-sway running?" >&2
	exit 1
fi
export WAYLAND_DISPLAY
export QT_QPA_PLATFORM=wayland

# on the phone, fill the screen with the Psion; an empty ROM argument keeps run.sh's default
[ $# -gt 0 ] || set -- ""
exec "$DIR/run.sh" "$@" --fullscreen
