#!/bin/sh
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
# usage: run.sh [ROM] [--cold-boot] [--cf CF_FOLDER_OR_IMAGE]
ROM="${1:-/home/user/roms/sys_rom.bin}"
[ $# -gt 0 ] && shift

# A CF card goes in the slot unless one is given: the folder in $WINDEMU_CF,
# or ~/psion-card. An empty $WINDEMU_CF leaves the slot empty.
case " $* " in
*" --cf "*) ;;
*)
	CF="${WINDEMU_CF-$HOME/psion-card}"
	if [ -n "$CF" ]; then
		[ -e "$CF" ] || mkdir -p "$CF"
		set -- "$@" --cf "$CF"
	fi
	;;
esac

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
