#!/bin/bash

iterations=5

MOUNT_POINT=$1
OUTPUT_DIR=$2
PM_DEVICE=$3
if [ -z $MOUNT_POINT ] || [ -z $OUTPUT_DIR ] || [ -z $PM_DEVICE ]; then 
    echo "Usage: run_filebench_tests.sh mountpoint output_dir pm_device"
    exit 1
fi
sudo mkdir -p $MOUNT_POINT
sudo mkdir -p $OUTPUT_DIR

# ensure workloads point at the mount directory
cp ./filebench/workloads/fileserver.f tests/fileserver.f
cp ./filebench/workloads/varmail.f tests/varmail.f
cp ./filebench/workloads/webserver.f tests/webserver.f
cp ./filebench/workloads/webproxy.f tests/webproxy.f

sed -i -e "s@$dir=/tmp@$dir=$MOUNT_POINT@" tests/fileserver.f
sed -i -e "s@$dir=/tmp@$dir=$MOUNT_POINT@" tests/varmail.f
sed -i -e "s@$dir=/tmp@$dir=$MOUNT_POINT@" tests/webserver.f
sed -i -e "s@$dir=/tmp@$dir=$MOUNT_POINT@" tests/webproxy.f

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
