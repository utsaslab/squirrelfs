#!/bin/bash 

iterations=10

MOUNT_POINT=$1
OUTPUT_DIR=$2
PM_DEVICE=$3
if [ -z $MOUNT_POINT ] || [ -z $OUTPUT_DIR ] || [ -z $PM_DEVICE ]; then 
    echo "Usage: run_remount_tests.sh mount_point output_dir pm_device"
    exit 1
fi 
mkdir -p $MOUNT_POINT
mkdir -p $OUTPUT_DIR

sudo -E scripts/remount_timing.sh squirrelfs $MOUNT_POINT init $OUTPUT_DIR $PM_DEVICE $iterations
sudo -E scripts/remount_timing.sh squirrelfs $MOUNT_POINT empty $OUTPUT_DIR $PM_DEVICE $iterations
sudo -E scripts/remount_timing.sh squirrelfs $MOUNT_POINT fill_device $OUTPUT_DIR $PM_DEVICE $iterations
