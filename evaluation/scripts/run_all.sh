#!/bin/bash
sim_threads=2

MOUNT_POINT=$1
OUTPUT_DIR=$2
PM_DEVICE=$3
if [ -z $MOUNT_POINT ] || [ -z $OUTPUT_DIR ] || [ -z $PM_DEVICE ]; then 
    echo "Usage: run_all.sh mountpoint output_dir pm_device"
    exit 1
fi
sudo mkdir -p $MOUNT_POINT
sudo mkdir -p $OUTPUT_DIR

sudo umount /dev/pmem0 > /dev/null 2>&1

sudo -E scripts/run_syscall_latency_tests.sh $MOUNT_POINT $OUTPUT_DIR $PM_DEVICE
sudo -E scripts/run_filebench_tests.sh $MOUNT_POINT $OUTPUT_DIR $PM_DEVICE
sudo -E scripts/run_rocksdb_tests.sh $MOUNT_POINT $OUTPUT_DIR $PM_DEVICE
sudo -E scripts/run_lmdb_tests.sh $MOUNT_POINT $OUTPUT_DIR $PM_DEVICE
sudo -E scripts/run_linux_checkout.sh $MOUNT_POINT $OUTPUT_DIR $PM_DEVICE
sudo -E scripts/run_remount_tests.sh $MOUNT_POINT $OUTPUT_DIR $PM_DEVICE
scripts/run_compilation_tests.sh $OUTPUT_DIR
scripts/run_model_sims.sh $sim_threads $OUTPUT_DIR

find "$OUTPUT_DIR" -print0 | xargs -0 chown "$SUDO_USER":"$SUDO_USER"
