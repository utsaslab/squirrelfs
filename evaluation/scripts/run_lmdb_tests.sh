#!/bin/bash 

op_count=100000000
iterations=5

MOUNT_POINT=$1
OUTPUT_DIR=$2
PM_DEVICE=$3
if [ -z $MOUNT_POINT ] | [ -z $OUTPUT_DIR] | [ -z $PM_DEVICE ]; then 
    echo "Usage: run_lmdb_tests.sh mountpoint pm_device"
    exit 1
fi
sudo mkdir -p $MOUNT_POINT
sudo mkdir -p $OUTPUT_DIR

sudo -E scripts/run_lmdb.sh nova $MOUNT_POINT fillseqbatch $OUTPUT_DIR $PM_DEVICE $op_count $iterations
sudo -E scripts/run_lmdb.sh squirrelfs $MOUNT_POINT fillseqbatch $OUTPUT_DIR $PM_DEVICE $op_count $iterations
sudo -E scripts/run_lmdb.sh ext4 $MOUNT_POINT fillseqbatch $OUTPUT_DIR $PM_DEVICE $op_count $iterations
sudo -E scripts/run_lmdb.sh winefs $MOUNT_POINT fillseqbatch $OUTPUT_DIR $PM_DEVICE $op_count $iterations

sudo -E scripts/run_lmdb.sh nova $MOUNT_POINT fillrandom $OUTPUT_DIR $PM_DEVICE $op_count $iterations
sudo -E scripts/run_lmdb.sh squirrelfs $MOUNT_POINT fillrandom $OUTPUT_DIR $PM_DEVICE $op_count $iterations
sudo -E scripts/run_lmdb.sh ext4 $MOUNT_POINT fillrandom $OUTPUT_DIR $PM_DEVICE $op_count $iterations
sudo -E scripts/run_lmdb.sh winefs $MOUNT_POINT fillrandom $OUTPUT_DIR $PM_DEVICE $op_count $iterations

sudo -E scripts/run_lmdb.sh nova $MOUNT_POINT fillrandbatch $OUTPUT_DIR $PM_DEVICE $op_count $iterations
sudo -E scripts/run_lmdb.sh squirrelfs $MOUNT_POINT fillrandbatch $OUTPUT_DIR $PM_DEVICE $op_count $iterations
sudo -E scripts/run_lmdb.sh ext4 $MOUNT_POINT fillrandbatch $OUTPUT_DIR $PM_DEVICE $op_count $iterations
sudo -E scripts/run_lmdb.sh winefs $MOUNT_POINT fillrandbatch $OUTPUT_DIR $PM_DEVICE $op_count $iterations