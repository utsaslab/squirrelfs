#!/bin/bash

MOUNT_POINT=$1
OUTPUT_DIR=$2
PM_DEVICE=$3

echo $MOUNT_POINT 
echo $OUTPUT_DIR 
echo $PM_DEVICE

if [ -z $MOUNT_POINT ] | [ -z $OUTPUT_DIR ] | [ -z $PM_DEVICE ]; then 
    echo "Usage: run_syscall_latency_tests.sh mountpoint output_dir pm_device"
    exit 1
fi
sudo mkdir -p $MOUNT_POINT
sudo mkdir -p $OUTPUT_DIR

sudo -E scripts/run_syscall_latency.sh nova $MOUNT_POINT $OUTPUT_DIR $PM_DEVICE
sudo -E scripts/run_syscall_latency.sh squirrelfs $MOUNT_POINT $OUTPUT_DIR $PM_DEVICE
sudo -E scripts/run_syscall_latency.sh ext4 $MOUNT_POINT $OUTPUT_DIR $PM_DEVICE
sudo -E scripts/run_syscall_latency.sh winefs $MOUNT_POINT $OUTPUT_DIR $PM_DEVICE