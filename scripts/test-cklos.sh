#!/bin/sh
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
IMAGE="$ROOT/build/cklos.img"
DATA_IMAGE="$ROOT/build/cklos-data.img"
LOG="$ROOT/build/qemu-test.log"
PERSIST_LOG="$ROOT/build/qemu-persist-test.log"
QEMU=${QEMU:-$(command -v qemu-system-x86_64 2>/dev/null || true)}

if [ -z "$QEMU" ]; then
    echo "qemu-system-x86_64 is required. Run: brew install qemu" >&2
    exit 1
fi

set +e
{
    sleep 2
    printf '\r'
    sleep 0.2
    printf 'sysinfo\r'
    sleep 0.3
    printf 'uptime\r'
    sleep 0.3
    printf 'memtest\r'
    sleep 0.3
    printf 'spawn worker\r'
    sleep 0.3
    printf 'tasks\r'
    sleep 0.3
    printf 'run DEMO.CKL\r'
    sleep 0.5
    printf 'cp MOTD.TXT COPY.TXT\r'
    sleep 0.2
    printf 'wc COPY.TXT\r'
    sleep 0.2
    printf 'head COPY.TXT 2\r'
    sleep 0.2
    printf 'tail COPY.TXT 1\r'
    sleep 0.2
    printf 'rm COPY.TXT\r'
    sleep 0.2
    printf 'kill 2\r'
    sleep 0.2
    printf 'write NOTES.TXT alpha beta\r'
    sleep 0.2
    printf 'append NOTES.TXT gamma\r'
    sleep 0.2
    printf 'cat NOTES.TXT\r'
    sleep 0.2
    printf 'mv NOTES.TXT ARCHIVE.TXT\r'
    sleep 0.2
    printf 'stat ARCHIVE.TXT\r'
    sleep 0.2
    printf 'rm ARCHIVE.TXT\r'
    sleep 0.2
    printf 'df\r'
    sleep 0.2
    printf 'history\r'
    sleep 0.2
    printf 'calc 42 * 7\r'
    sleep 0.2
    printf 'write PERSIST.TXT persistence survives reboot\r'
    sleep 0.2
    printf 'disksave\r'
    sleep 0.3
    printf 'shutdown\r'
    sleep 1
} | "$QEMU" \
    -m 64M \
    -cpu max \
    -drive "file=$IMAGE,format=raw,if=floppy" \
    -boot a \
    -drive "file=$DATA_IMAGE,format=raw,if=ide,index=0" \
    -netdev user,id=net0 \
    -device rtl8139,netdev=net0,mac=52:54:00:12:34:56 \
    -display none \
    -serial stdio \
    -monitor none \
    -no-reboot \
    -device isa-debug-exit >"$LOG" 2>&1
status=$?
set -e

cat "$LOG"

if [ "$status" -ne 33 ] && [ "$status" -ne 0 ]; then
    echo "QEMU exited with unexpected status $status" >&2
    exit 1
fi

for expected in \
    'CKLOS 0.6.0 kernel is running' \
    'CKLOS>' \
    'OS        CKLOS 0.6.0' \
    'PIT 100 Hz' \
    'Uptime' \
    'Heap allocator: PASS' \
    'Page allocator: PASS' \
    'Spawned worker task' \
    'worker-' \
    'Running DEMO.CKL' \
    'CKLOS script engine is active.' \
    'Copied MOTD.TXT to COPY.TXT' \
    '3 lines  18 words  120 bytes  COPY.TXT' \
    'A compact command-line operating system built from scratch.' \
    'Type HELP to see every available command.' \
    'Removed COPY.TXT.' \
    'Stopped task 2.' \
    'WRITE: NOTES.TXT now contains 10 bytes' \
    'alpha betagamma' \
    'Renamed NOTES.TXT to ARCHIVE.TXT' \
    'Mode      read/write' \
    'Removed ARCHIVE.TXT' \
    'RAM disk' \
    'history' \
    '294' \
    'Saved 1 user file(s)' \
    'Shutting down CKLOS'
do
    if ! grep -F "$expected" "$LOG" >/dev/null; then
        echo "Test failed: missing '$expected'" >&2
        exit 1
    fi
done

set +e
{
    sleep 2
    printf '\r'
    sleep 0.2
    printf 'disk\r'
    sleep 0.3
    printf 'cat PERSIST.TXT\r'
    sleep 0.3
    printf 'shutdown\r'
    sleep 1
} | "$QEMU" \
    -m 64M \
    -cpu max \
    -drive "file=$IMAGE,format=raw,if=floppy" \
    -boot a \
    -drive "file=$DATA_IMAGE,format=raw,if=ide,index=0" \
    -netdev user,id=net0 \
    -device rtl8139,netdev=net0,mac=52:54:00:12:34:56 \
    -display none \
    -serial stdio \
    -monitor none \
    -no-reboot \
    -device isa-debug-exit >"$PERSIST_LOG" 2>&1
status=$?
set -e

cat "$PERSIST_LOG"

if [ "$status" -ne 33 ] && [ "$status" -ne 0 ]; then
    echo "Persistence test: unexpected QEMU status $status" >&2
    exit 1
fi

for expected in \
    'Saved     1 user file(s)' \
    'persistence survives reboot'
do
    if ! grep -F "$expected" "$PERSIST_LOG" >/dev/null; then
        echo "Persistence test failed: missing '$expected'" >&2
        exit 1
    fi
done

echo "CKLOS boot, kernel, scheduler, storage and shell tests passed."
