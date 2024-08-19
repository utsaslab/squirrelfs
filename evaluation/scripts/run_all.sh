#!/bin/bash
sim_threads=2

$(dirname "$0")/precheck.sh || exit 1

create_output_dir() {
    mkdir -p $1 
    if [ $? != 0 ]; then 
        echo "Could not create $OUTPUT_DIR"
        exit 1
    fi
}

MOUNT_POINT=$1
OUTPUT_DIR=$2
PM_DEVICE=$3
if [ -z $MOUNT_POINT ] || [ -z $OUTPUT_DIR ] || [ -z $PM_DEVICE ]; then 
    echo "Usage: run_all.sh mountpoint output_dir pm_device"
    exit 1
fi
sudo mkdir -p $MOUNT_POINT

# create per-fs output directories and make sure they are accessible without sudo
create_output_dir $OUTPUT_DIR
create_output_dir $OUTPUT_DIR/nova 
create_output_dir $OUTPUT_DIR/squirrelfs 
create_output_dir $OUTPUT_DIR/ext4
create_output_dir $OUTPUT_DIR/winefs
sudo chown -R "${SUDO_USER:-$USER}":"${SUDO_USER:-$USER}" $OUTPUT_DIR

sudo umount /dev/pmem0 > /dev/null 2>&1

sudo -E scripts/run_syscall_latency_tests.sh $MOUNT_POINT $OUTPUT_DIR $PM_DEVICE
sudo -E scripts/run_filebench_tests.sh $MOUNT_POINT $OUTPUT_DIR $PM_DEVICE
sudo -E scripts/run_rocksdb_tests.sh $MOUNT_POINT $OUTPUT_DIR $PM_DEVICE
sudo -E scripts/run_lmdb_tests.sh $MOUNT_POINT $OUTPUT_DIR $PM_DEVICE
sudo -E scripts/run_linux_checkout.sh $MOUNT_POINT $OUTPUT_DIR $PM_DEVICE
sudo -E scripts/run_remount_tests.sh $MOUNT_POINT $OUTPUT_DIR $PM_DEVICE
scripts/run_compilation_tests.sh $OUTPUT_DIR
scripts/run_model_sims.sh $sim_threads $OUTPUT_DIR

find "$OUTPUT_DIR" -print0 | xargs -0 sudo chown -R "$SUDO_USER":"$SUDO_USER"
