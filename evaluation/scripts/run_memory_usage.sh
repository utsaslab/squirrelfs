#!/bin/bash

iterations=1

MOUNT_POINT=$1
OUTPUT_DIR=$2
PM_DEVICE=$3
if [ -z $MOUNT_POINT ] || [ -z $OUTPUT_DIR ] || [ -z $PM_DEVICE ]; then 
    echo "Usage: run_remount_tests.sh mount_point output_dir pm_device"
    exit 1
fi 
mkdir -p $MOUNT_POINT
mkdir -p $OUTPUT_DIR

# sudo -E scripts/memory_usage.sh squirrelfs $MOUNT_POINT init $OUTPUT_DIR $PM_DEVICE $iterations
# sudo -E scripts/memory_usage.sh ext4 $MOUNT_POINT init $OUTPUT_DIR $PM_DEVICE $iterations
# sudo -E scripts/memory_usage.sh nova $MOUNT_POINT init $OUTPUT_DIR $PM_DEVICE $iterations
# sudo -E scripts/memory_usage.sh winefs $MOUNT_POINT init $OUTPUT_DIR $PM_DEVICE $iterations

# sudo -E scripts/memory_usage.sh squirrelfs $MOUNT_POINT empty $OUTPUT_DIR $PM_DEVICE $iterations
# sudo -E scripts/memory_usage.sh ext4 $MOUNT_POINT empty $OUTPUT_DIR $PM_DEVICE $iterations
# sudo -E scripts/memory_usage.sh nova $MOUNT_POINT empty $OUTPUT_DIR $PM_DEVICE $iterations
# sudo -E scripts/memory_usage.sh winefs $MOUNT_POINT empty $OUTPUT_DIR $PM_DEVICE $iterations

sudo -E scripts/memory_usage.sh squirrelfs $MOUNT_POINT full $OUTPUT_DIR $PM_DEVICE $iterations
# sudo -E scripts/memory_usage.sh ext4 $MOUNT_POINT full $OUTPUT_DIR $PM_DEVICE $iterations
# sudo -E scripts/memory_usage.sh nova $MOUNT_POINT full $OUTPUT_DIR $PM_DEVICE $iterations
# sudo -E scripts/memory_usage.sh winefs $MOUNT_POINT full $OUTPUT_DIR $PM_DEVICE $iterations