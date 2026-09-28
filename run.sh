#!/bin/sh
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
# usage: run.sh [ROM] [--cold-boot]
ROM="${1:-/home/user/roms/sys_rom.bin}"
[ $# -gt 0 ] && shift

# The interpreter is single-threaded and runs ~2x faster on big cores
bigcores=""
maxfreq=0
for f in /sys/devices/system/cpu/cpu[0-9]*/cpufreq/cpuinfo_max_freq; do
	[ -r "$f" ] || continue
	freq=$(cat "$f")
	cpu=${f#/sys/devices/system/cpu/cpu}; cpu=${cpu%%/*}
	if [ "$freq" -gt "$maxfreq" ]; then
		maxfreq=$freq; bigcores=$cpu
	elif [ "$freq" -eq "$maxfreq" ]; then
		bigcores="$bigcores,$cpu"
	fi
done

if [ -n "$bigcores" ] && command -v taskset >/dev/null 2>&1; then
	exec taskset -c "$bigcores" "$DIR/WindQt/WindQt" "$@" "$ROM"
fi
exec "$DIR/WindQt/WindQt" "$@" "$ROM"
