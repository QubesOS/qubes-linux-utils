#!/bin/bash --
set -euo pipefail

case $0 in (/*) cd "${0%/*}/";; (*/*) cd "./${0%/*}";; (*) :;; esac
make -C .. gptfixer/gpt
chk () {
    loopdev=$(sudo losetup --nooverlap --find --sector-size "$1" --show -- dummy.img)
    if [[ "$loopdev" != '/dev/loop0' ]]; then
        printf 'Loop device is not /dev/loop0 (got %q), expect test failure\n' "$loopdev"
    fi >&3
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
    sfdisk --force dummy.img < layout
    chk 4096
    chk 512
)

case "$#,${1-}" in
('1,update') go 3>&2 > test.sh.stdout 2> test.sh.stderr;;
(0,)
    tmpdir=$(mktemp -d)
    go 3>&2 > "$tmpdir/stdout" 2> "$tmpdir/stderr"
    diff -u -- "$tmpdir/stdout" test.sh.stdout
    diff -u -- "$tmpdir/stderr" test.sh.stderr
    rm -rf -- "$tmpdir"
;;
(*) echo "Usage: test.sh [update]" >&2; exit 1;;
esac
