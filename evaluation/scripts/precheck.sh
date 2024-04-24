#!/bin/bash
set -e

PREFIX="[precheck]"

msg() {
    echo "$PREFIX $1"
}

err() {
    echo "$PREFIX ERROR: $1"
    exit 1
}

# Relative paths assume that we begin in the evaluation/ directory.
msg "working directory"
CWD=$(basename -- $PWD)
if [[ "$CWD" != "evaluation" ]]; then
    err "Eval scripts expect to be run from the evaluation/ directory."
fi

# Did we remember to install special bits?
for PROG in numactl java rustc; do
    msg "$PROG"
    if ! command -v $PROG; then
        err "Missing $PROG on our \$PATH (did you run dependencies.sh?)"
    fi
done

# Have we correctly built all the kernel modules?
LINUX_BASEDIR=../linux
for FS in nova squirrelfs ext4 winefs; do
    msg "$FS module"
    FS_PATH="$LINUX_BASEDIR"/fs/$FS/$FS.ko
    if ! test -f $"LINUX_BASEDIR"/fs/$FS/$FS.ko; then
        err "Missing kernel module for $FS at $FS_PATH; did linux build correctly?"
    fi
done

# did we install the right kernel?
msg "uname"
if [[ ! $(uname -r) =~ "squirrelfs" ]]; then
    err "Unexpected non-squirrelfs kernel name; did you run 'sudo make modules_install install' in ../linux/?"
fi

# Did we remember to build benchmark stuff?
for EXE in "filebench/filebench" "lmdb/dbbench/bin/t_lmdb"; do
    msg $(basename -- $EXE)
    if ! test -f "$EXE"; then
        err "$EXE missing; did you run build_benchmarks.sh or did compilation fail?"
    fi
done

msg "ok!"
exit 0
