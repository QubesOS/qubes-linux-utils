#!/bin/bash --
set -euo pipefail

case $0 in (/*) cd "${0%/*}/";; (*/*) cd "./${0%/*}";; (*) :;; esac
make gpt

ensure_loop_nodes () {
    local n
    for n in $(seq 0 15); do
        [[ -e /dev/loop$n ]] && continue
        if ! sudo mknod /dev/loop$n b 7 "$n" 2>/dev/null; then
            echo "Cannot create /dev/loop$n; runner needs CAP_MKNOD or privileged" >&2
            return 1
        fi
    done
}

chk () {
    if ! loopdev=$(sudo losetup --nooverlap --find --sector-size "$1" --show -- dummy.img); then
        echo "losetup --find failed (kernel allocated a loop index whose /dev node is missing in this container)" >&2
        return 1
    fi
    echo Dumping broken partition table
    sudo sfdisk --label=gpt --dump -- "$loopdev"
    sudo ./gpt fix "$loopdev"
    echo Dumping fixed partition table
    sudo sfdisk --label=gpt --dump -- "$loopdev"
    sudo losetup -d "$loopdev"
}

go () (
    set -x
    truncate -s 0 dummy.img
    truncate -s 20GiB dummy.img
    sfdisk --force dummy.img < layout | grep -v "^Syncing disks"
    chk 4096
    chk 512
)

normalize_loop () {
    sed -E 's|/dev/loop[0-9]+|/dev/loop0|g'
}

case "$#,${1-}" in
('1,update')
    ensure_loop_nodes
    tmpdir=$(mktemp -d)
    go 3>&2 > "$tmpdir/stdout" 2> "$tmpdir/stderr"
    normalize_loop < "$tmpdir/stdout" > test.sh.stdout
    normalize_loop < "$tmpdir/stderr" > test.sh.stderr
    rm -rf -- "$tmpdir"
;;
(0,)
    ensure_loop_nodes
    tmpdir=$(mktemp -d)
    go 3>&2 > "$tmpdir/stdout" 2> "$tmpdir/stderr" || { cat "$tmpdir/stderr"; exit 1; }
    diff -u <(normalize_loop < "$tmpdir/stdout") test.sh.stdout
    diff -u <(normalize_loop < "$tmpdir/stderr") test.sh.stderr
    rm -rf -- "$tmpdir"
;;
(*) echo "Usage: test.sh [update]" >&2; exit 1;;
esac
