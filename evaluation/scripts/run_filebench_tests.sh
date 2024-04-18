#!/bin/bash

iterations=5

MOUNT_POINT=$1
OUTPUT_DIR=$2
PM_DEVICE=$3
if [ -z $MOUNT_POINT ] | [ -z $OUTPUT_DIR ] | [ -z $PM_DEVICE ]; then 
    echo "Usage: run_filebench_tests.sh mountpoint output_dir pm_device"
    exit 1
fi
sudo mkdir -p $MOUNT_POINT
sudo mkdir -p $OUTPUT_DIR

sudo -E ./scripts/run_filebench.sh squirrelfs $MOUNT_POINT fileserver $OUTPUT_DIR $PM_DEVICE $iterations
sudo -E ./scripts/run_filebench.sh nova $MOUNT_POINT fileserver $OUTPUT_DIR $PM_DEVICE $iterations
sudo -E ./scripts/run_filebench.sh winefs $MOUNT_POINT fileserver $OUTPUT_DIR $PM_DEVICE $iterations
sudo -E ./scripts/run_filebench.sh ext4 $MOUNT_POINT fileserver $OUTPUT_DIR $PM_DEVICE $iterations

sudo -E ./scripts/run_filebench.sh squirrelfs $MOUNT_POINT varmail $OUTPUT_DIR $PM_DEVICE $iterations
sudo -E ./scripts/run_filebench.sh nova $MOUNT_POINT varmail $OUTPUT_DIR $PM_DEVICE $iterations
sudo -E ./scripts/run_filebench.sh winefs $MOUNT_POINT varmail $OUTPUT_DIR $PM_DEVICE $iterations
sudo -E ./scripts/run_filebench.sh ext4 $MOUNT_POINT varmail $OUTPUT_DIR $PM_DEVICE $iterations

sudo -E ./scripts/run_filebench.sh squirrelfs $MOUNT_POINT webserver $OUTPUT_DIR $PM_DEVICE $iterations
sudo -E ./scripts/run_filebench.sh nova $MOUNT_POINT webserver $OUTPUT_DIR $PM_DEVICE $iterations
sudo -E ./scripts/run_filebench.sh winefs $MOUNT_POINT webserver $OUTPUT_DIR $PM_DEVICE $iterations
sudo -E ./scripts/run_filebench.sh ext4 $MOUNT_POINT webserver $OUTPUT_DIR $PM_DEVICE $iterations

sudo -E ./scripts/run_filebench.sh squirrelfs $MOUNT_POINT webproxy $OUTPUT_DIR $PM_DEVICE $iterations
sudo -E ./scripts/run_filebench.sh nova $MOUNT_POINT webproxy $OUTPUT_DIR $PM_DEVICE $iterations
sudo -E ./scripts/run_filebench.sh winefs $MOUNT_POINT webproxy $OUTPUT_DIR $PM_DEVICE $iterations
sudo -E ./scripts/run_filebench.sh ext4 $MOUNT_POINT webproxy $OUTPUT_DIR $PM_DEVICE $iterations
