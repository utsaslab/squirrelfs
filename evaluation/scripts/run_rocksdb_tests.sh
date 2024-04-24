#!/bin/bash

operation_count=25000000
record_count=25000000
num_threads=8
iterations=5

MOUNT_POINT=$1
OUTPUT_DIR=$2
PM_DEVICE=$3
if [ -z $MOUNT_POINT ] || [ -z $OUTPUT_DIR ] || [ -z $PM_DEVICE ]; then 
    echo "Usage: run_rocksdb_tests.sh mountpoint output_dir pm_device"
    exit 1
fi
sudo mkdir -p $MOUNT_POINT

sudo -E scripts/run_rocksdb.sh nova $MOUNT_POINT $OUTPUT_DIR $PM_DEVICE $operation_count $record_count $num_threads $iterations
sudo -E scripts/run_rocksdb.sh squirrelfs $MOUNT_POINT $OUTPUT_DIR $PM_DEVICE $operation_count $record_count $num_threads $iterations
sudo -E scripts/run_rocksdb.sh ext4 $MOUNT_POINT $OUTPUT_DIR $PM_DEVICE $operation_count $record_count $num_threads $iterations
sudo -E scripts/run_rocksdb.sh winefs $MOUNT_POINT $OUTPUT_DIR $PM_DEVICE $operation_count $record_count $num_threads $iterations
