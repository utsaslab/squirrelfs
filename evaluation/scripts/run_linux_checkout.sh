#!/bin/bash

iterations=10

MOUNT_POINT=$1
OUTPUT_DIR=$2
PM_DEVICE=$3
if [ -z $MOUNT_POINT] | [ -z $OUTPUT_DIR ] | [ -z $PM_DEVICE ]; then 
    echo "Usage: run_linux_checkout.sh mount_point output_dir pm_device"
    exit 1
fi 
mkdir -p $OUTPUT_DIR

scripts/linux_checkout.sh nova $OUTPUT_DIR $iterations
scripts/linux_checkout.sh squirrelfs $OUTPUT_DIR $iterations
scripts/linux_checkout.sh ext4 $OUTPUT_DIR $iterations
scripts/linux_checkout.sh winefs $OUTPUT_DIR $iterations
