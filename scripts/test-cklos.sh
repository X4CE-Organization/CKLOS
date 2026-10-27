#!/bin/sh
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
IMAGE="$ROOT/build/cklos.img"
DATA_IMAGE="$ROOT/build/cklos-data.img"
FS2_IMAGE="$ROOT/build/cklos-fs2.img"
TEST_IMAGE="$ROOT/build/cklos-test.img"
TEST_DATA_IMAGE="$ROOT/build/cklos-test-data.img"
TEST_FS2_IMAGE="$ROOT/build/cklos-test-fs2.img"
LOG="$ROOT/build/qemu-test.log"
PERSIST_LOG="$ROOT/build/qemu-persist-test.log"
IPV6_LOG="$ROOT/build/qemu-ipv6-test.log"
QEMU=${QEMU:-$(command -v qemu-system-x86_64 2>/dev/null || true)}

if [ -z "$QEMU" ]; then
    echo "qemu-system-x86_64 is required. Run: brew install qemu" >&2
    exit 1
fi

python3 "$ROOT/scripts/http-test-server.py" "$ROOT/programs" \
    --port 18081 >"$ROOT/build/http-test-server.log" 2>&1 &
HTTP_PID=$!
python3 "$ROOT/scripts/http-test-server.py" "$ROOT/programs" \
    --port 18444 --cert "$ROOT/build/test-cert.pem" \
    --key "$ROOT/build/test-key.pem" \
    >"$ROOT/build/https-test-server.log" 2>&1 &
HTTPS_PID=$!
trap 'kill $HTTP_PID $HTTPS_PID ${IPV6_PID:-} ${SSH_QEMU_PID:-} 2>/dev/null || true' EXIT HUP INT TERM
sleep 1

dd if=/dev/zero of="$TEST_IMAGE" bs=512 count=2880 2>/dev/null
dd if="$ROOT/build/boot.bin" of="$TEST_IMAGE" conv=notrunc bs=512 seek=0 2>/dev/null
dd if="$ROOT/build/stage2.bin" of="$TEST_IMAGE" conv=notrunc bs=512 seek=1 2>/dev/null
dd if="$ROOT/build/kernel.bin" of="$TEST_IMAGE" conv=notrunc bs=512 seek=33 2>/dev/null
if [ -f "$DATA_IMAGE" ]; then
    cp "$DATA_IMAGE" "$TEST_DATA_IMAGE"
else
    dd if=/dev/zero of="$TEST_DATA_IMAGE" bs=512 count=4096 2>/dev/null
fi
python3 "$ROOT/tools/install-rks-disk.py" \
    "$TEST_DATA_IMAGE" "$ROOT/build/HELLO.RKS" HELLO.RKS
if [ ! -f "$FS2_IMAGE" ]; then
    echo "$FS2_IMAGE is required" >&2
    exit 1
fi
cp "$FS2_IMAGE" "$TEST_FS2_IMAGE"

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
    printf 'exec HELLO.RKS\r'
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
    printf 'fsmkdir /home/cklos/deep\r'
    sleep 0.2
    printf 'fsmkdir /home/cklos/deep/中文\r'
    sleep 0.2
    printf 'fswrite /home/cklos/deep/中文/你好.txt 你好 CKLOS\r'
    sleep 0.2
    printf 'fsfill /home/cklos/deep/large.bin 32768\r'
    sleep 0.6
    printf 'fsmv /home/cklos/deep/large.bin /home/cklos/deep/renamed.bin\r'
    sleep 0.2
    printf 'fsmigrate\r'
    sleep 0.3
    printf 'fscheck\r'
    sleep 0.3
    printf 'fscat /import/PERSIST.TXT\r'
    sleep 0.2
    printf 'fscat /home/cklos/deep/中文/你好.txt\r'
    sleep 0.2
    printf 'fswrite /home/cklos/cki-test.txt 你好 CKLOS editor\r'
    sleep 0.3
    printf 'fscat /home/cklos/cki-test.txt\r'
    sleep 0.3
    printf 'curl -v -o /home/cklos/https-download.c https://10.0.2.2:18444/cck_hello.c\r'
    sleep 12
    printf 'cwgt -q -O /home/cklos/http-download.c http://10.0.2.2:18081/cck_hello.c\r'
    sleep 8
    printf 'curl -L -s -o /home/cklos/redirect.c http://10.0.2.2:18081/redirect\r'
    sleep 4
    printf 'curl -s -d hello -o /home/cklos/post.txt http://10.0.2.2:18081/post\r'
    sleep 4
    printf 'curl -s -u user:pass -o /home/cklos/auth.txt http://10.0.2.2:18081/auth\r'
    sleep 4
    printf 'curl -s --range 4-7 -o /home/cklos/range.txt http://10.0.2.2:18081/range\r'
    sleep 4
    printf 'curl -s -A CKLOS-agent -H X-CKLOS-Test:yes -o /home/cklos/echo.txt http://10.0.2.2:18081/echo\r'
    sleep 4
    printf 'fscat /home/cklos/http-download.c\r'
    sleep 0.3
    printf 'fscat /home/cklos/redirect.c\r'
    sleep 0.3
    printf 'fscat /home/cklos/post.txt\r'
    sleep 0.3
    printf 'fscat /home/cklos/auth.txt\r'
    sleep 0.3
    printf 'fscat /home/cklos/range.txt\r'
    sleep 0.3
    printf 'fscat /home/cklos/echo.txt\r'
    sleep 0.3
    printf 'fscat /home/cklos/https-download.c\r'
    sleep 0.3
    printf 'elfexec /bin/USERHELLO.ELF\r'
    sleep 0.8
    printf 'elfexec /bin/USERFORK.ELF\r'
    sleep 1
    printf 'elfexec /bin/USERFAULT.ELF\r'
    sleep 0.5
    printf 'elfexec /bin/USEREXEC.ELF\r'
    sleep 1
    printf 'elfexec /bin/USERIO.ELF\r'
    sleep 0.8
    printf 'elfexec /bin/USERPIPE.ELF\r'
    sleep 1
    printf 'elfexec /bin/MUSLHELLO.ELF\r'
    sleep 1
    printf 'cck -v\r'
    sleep 0.2
    printf 'cck -std=c98 /home/cklos/cck_hello.c\r'
    sleep 0.2
    printf 'cck -std=c89 -c -o /home/cklos/cklos-c89.o /home/cklos/cck_hello.c\r'
    sleep 1
    printf 'cck -std=gnu89 -c -o /home/cklos/cklos-gnu89.o /home/cklos/cck_hello.c\r'
    sleep 1
    printf 'cck -std=c99 -c -o /home/cklos/cklos-c99.o /home/cklos/cck_hello.c\r'
    sleep 1
    printf 'cck -std=gnu99 -c -o /home/cklos/cklos-gnu99.o /home/cklos/cck_hello.c\r'
    sleep 1
    printf 'cck -std=c11 -c -o /home/cklos/cklos-c11.o /home/cklos/cck_hello.c\r'
    sleep 1
    printf 'cck -std=gnu11 -c -o /home/cklos/cklos-gnu11.o /home/cklos/cck_hello.c\r'
    sleep 1
    printf 'cck -std=c17 -c -o /home/cklos/cklos-c17.o /home/cklos/cck_hello.c\r'
    sleep 1
    printf 'cck -std=gnu17 -c -o /home/cklos/cklos-gnu17.o /home/cklos/cck_hello.c\r'
    sleep 1
    printf 'cck -std=c23 -c -o /home/cklos/cklos-c23.o /home/cklos/cck_hello.c\r'
    sleep 1
    printf 'cck -std=gnu23 -c -o /home/cklos/cklos-gnu23.o /home/cklos/cck_hello.c\r'
    sleep 1
    printf 'fsls /home/cklos\r'
    sleep 0.2
    printf 'cck /home/cklos/cck_hello.c\r'
    sleep 12
    printf 'elfexec /a.rks\r'
    sleep 4
    printf 'fscat /home/cklos/user-io.txt\r'
    sleep 0.2
    printf 'shutdown\r'
    sleep 1
} | "$QEMU" \
    -m 64M \
    -cpu max \
    -drive "file=$TEST_IMAGE,format=raw,if=floppy" \
    -boot a \
    -drive "file=$TEST_DATA_IMAGE,format=raw,if=ide,index=0" \
    -drive "file=$TEST_FS2_IMAGE,format=raw,if=ide,index=1" \
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
    'CKLOS 0.15.0 kernel is running' \
    'CKLOS>' \
    'OS        CKLOS 0.15.0' \
    'PIT 100 Hz' \
    'Uptime' \
    'Heap allocator: PASS' \
    'Page allocator: PASS' \
    'Spawned worker task' \
    'worker-' \
    'Running DEMO.CKL' \
    'CKLOS script engine is active.' \
    'RKS executable is running!' \
    'RKS programs are standalone binaries loaded at runtime.' \
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
    'Saved 2 user file(s)' \
    'Directory created.' \
    'FSFILL: created /home/cklos/deep/large.bin with 32768 bytes.' \
    'FSMIGRATE: imported ' \
    'FSCHECK: metadata valid' \
    'persistence survives reboot' \
    '你好 CKLOS' \
    'VBE linear 1024x768x32' \
    '你好 CKLOS editor' \
    'TLS handshake/read complete' \
    'HTTP 200  112 bytes -> /home/cklos/https-download.c' \
    'posted:hello' \
    'authenticated' \
    '4567' \
    'method=GET' \
    'user-agent=CKLOS-agent' \
    'header=yes' \
    'cck compiled this ELF64 program on CKLOS.' \
    'CKLOS ring-3 ELF64 user program is running.' \
    'Cooperative user-mode system call round trip complete.' \
    'child: fork returned 0' \
    'parent: waited pid' \
    'terminated by exception 14' \
    'execve and wait completed' \
    'read back: CKLOS user file I/O' \
    'parent read: pipe across fork' \
    'CKLOS musl static libc is running.' \
    'pid=' \
    'cck 0.10 (TinyCC backend 0.9.28rc)' \
    'CCK: C98 is not a C standard.' \
    'cklos-c89.o' \
    'cklos-gnu89.o' \
    'cklos-c99.o' \
    'cklos-gnu99.o' \
    'cklos-c11.o' \
    'cklos-gnu11.o' \
    'cklos-c17.o' \
    'cklos-gnu17.o' \
    'cklos-c23.o' \
    'cklos-gnu23.o' \
    'CCK: wrote verified RKS2 executable a.rks.' \
    'cck compiled this ELF64 program on CKLOS.' \
    'Shutting down CKLOS'
do
    if ! grep -F "$expected" "$LOG" >/dev/null; then
        echo "Test failed: missing '$expected'" >&2
        exit 1
    fi
done

SSH_LOG="$ROOT/build/qemu-ssh-test.log"
SSH_PORT=${SSH_TEST_PORT:-2222}
(
    sleep 20
    printf 'shutdown\r'
    sleep 1
) | "$QEMU" \
    -m 64M \
    -cpu max \
    -drive "file=$TEST_IMAGE,format=raw,if=floppy" \
    -boot a \
    -drive "file=$TEST_DATA_IMAGE,format=raw,if=ide,index=0" \
    -drive "file=$TEST_FS2_IMAGE,format=raw,if=ide,index=1" \
    -netdev "user,id=net0,hostfwd=tcp:127.0.0.1:$SSH_PORT-:22" \
    -device rtl8139,netdev=net0,mac=52:54:00:12:34:56 \
    -display none -serial stdio -monitor none -no-reboot \
    -device isa-debug-exit >"$SSH_LOG" 2>&1 &
SSH_QEMU_PID=$!
sleep 2
if ! ssh -i "$ROOT/keys/cklos_user" -p "$SSH_PORT" \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o BatchMode=yes -o ConnectTimeout=5 -T \
    cklos@127.0.0.1 uname >"$ROOT/build/ssh-client.log" 2>&1; then
    cat "$ROOT/build/ssh-client.log" >&2
    cat "$SSH_LOG" >&2
    kill "$SSH_QEMU_PID" 2>/dev/null || true
    exit 1
fi
grep -F 'CKLOS' "$ROOT/build/ssh-client.log" >/dev/null
cat "$ROOT/build/ssh-client.log"
wait "$SSH_QEMU_PID" 2>/dev/null || true

python3 "$ROOT/scripts/ipv6-test-peer.py" \
    >"$ROOT/build/ipv6-test-peer.log" 2>&1 &
IPV6_PID=$!
sleep 0.5
set +e
{
    sleep 2
    printf '\r'
    sleep 0.2
    printf 'curl -v -o /home/cklos/ipv6.c http://[fec0::2]:18082/cck_hello.c\r'
    sleep 8
    printf 'fscat /home/cklos/ipv6.c\r'
    sleep 0.3
    printf 'shutdown\r'
    sleep 1
} | "$QEMU" \
    -m 64M \
    -cpu max \
    -drive "file=$TEST_IMAGE,format=raw,if=floppy" \
    -boot a \
    -drive "file=$TEST_DATA_IMAGE,format=raw,if=ide,index=0" \
    -drive "file=$TEST_FS2_IMAGE,format=raw,if=ide,index=1" \
    -netdev socket,id=net0,connect=127.0.0.1:19090 \
    -device rtl8139,netdev=net0,mac=52:54:00:12:34:56 \
    -display none \
    -serial stdio \
    -monitor none \
    -no-reboot \
    -device isa-debug-exit >"$IPV6_LOG" 2>&1
status=$?
set -e
kill "$IPV6_PID" 2>/dev/null || true
cat "$IPV6_LOG"
if [ "$status" -ne 33 ] && [ "$status" -ne 0 ]; then
    echo "IPv6 test: unexpected QEMU status $status" >&2
    exit 1
fi
for expected in \
    'HTTP 200  53 bytes -> /home/cklos/ipv6.c' \
    '#include <stdio.h>' \
    'Shutting down CKLOS'
do
    if ! grep -F "$expected" "$IPV6_LOG" >/dev/null; then
        echo "IPv6 test failed: missing '$expected'" >&2
        exit 1
    fi
done

set +e
{
    sleep 2
    printf '\r'
    sleep 0.2
    printf 'cki /home/cklos/cki-interactive.txt\r'
    sleep 2
    printf '你好\r'
    sleep 5
    printf '\023'
    sleep 1
    printf '\021'
    sleep 2
    printf 'fscat /home/cklos/cki-interactive.txt\r'
    sleep 0.3
    printf 'disk\r'
    sleep 0.3
    printf 'cat PERSIST.TXT\r'
    sleep 0.3
    printf 'fscheck\r'
    sleep 0.3
    printf 'fscat /import/PERSIST.TXT\r'
    sleep 0.3
    printf 'fscat /home/cklos/deep/中文/你好.txt\r'
    sleep 0.3
    printf 'fsls /home/cklos/deep\r'
    sleep 0.3
    printf 'fscat /home/cklos/cki-test.txt\r'
    sleep 0.3
    printf 'shutdown\r'
    sleep 1
} | "$QEMU" \
    -m 64M \
    -cpu max \
    -drive "file=$TEST_IMAGE,format=raw,if=floppy" \
    -boot a \
    -drive "file=$TEST_DATA_IMAGE,format=raw,if=ide,index=0" \
    -drive "file=$TEST_FS2_IMAGE,format=raw,if=ide,index=1" \
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
    'Saved     2 user file(s)' \
    'persistence survives reboot' \
    'FSCHECK: metadata valid' \
    '你好 CKLOS' \
    'renamed.bin  32768 bytes' \
    '你好' \
    '你好 CKLOS editor'
do
    if ! grep -F "$expected" "$PERSIST_LOG" >/dev/null; then
        echo "Persistence test failed: missing '$expected'" >&2
        exit 1
    fi
done

echo "CKLOS boot, kernel, scheduler, storage and shell tests passed."
