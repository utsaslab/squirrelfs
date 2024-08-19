#!/bin/bash

FS=$1
MOUNT_POINT=$2
workload=$3
OUTPUT_DIR=$4
PM_DEVICE=$5
OP_COUNT=$6
ITERATIONS=$7
output_dir=$OUTPUT_DIR
# iterations=5

if [ -z $FS ] || [ -z $MOUNT_POINT ] || [ -z $workload ] || [ -z $OUTPUT_DIR ] || [ -z $PM_DEVICE ] || [ -z $OP_COUNT ] || [ -z $ITERATIONS ]; then 
    echo "Usage: run_lmdb.sh fs mountpoint test output_dir pm_device operation_count iterations"
    exit 1
fi
sudo mkdir -p $MOUNT_POINT
sudo mkdir -p $OUTPUT_DIR

# op_count=100000000

mkdir -p $output_dir/$FS/lmdb/$workload

if [ $FS = "arckfs" ]; then 
    sudo ndctl create-namespace -f -e namespace0.0 --mode=devdax
else 
    sudo ndctl create-namespace -f -e namespace0.0 --mode=fsdax
fi

for i in $(seq $ITERATIONS)
do
    if [ $FS = "squirrelfs" ]; then 
        sudo -E insmod ../linux/fs/squirrelfs/squirrelfs.ko; sudo mount -t squirrelfs -o init $PM_DEVICE $MOUNT_POINT/
    elif [ $FS = "nova" ]; then 
        sudo -E insmod ../linux/fs/nova/nova.ko; sudo mount -t NOVA -o init $PM_DEVICE $MOUNT_POINT/
    elif [ $FS = "winefs" ]; then 
        sudo -E insmod ../linux/fs/winefs/winefs.ko; sudo mount -t winefs -o init $PM_DEVICE $MOUNT_POINT/
    elif [ $FS = "ext4" ]; then 
        yes | sudo mkfs.ext4 $PM_DEVICE 
        sudo -E mount -t ext4 -o dax $PM_DEVICE $MOUNT_POINT/
    elif [ $FS = "arckfs" ]; then 
        sudo insmod arckfs/kfs/sufs.ko pm_nr=1
        sudo chmod 666 /dev/supremefs
        sudo arckfs/fsutils/init
    fi 

    

    if [ $FS = "arckfs" ]; then 
        numactl --membind=0 lmdb/dbbench/bin/t_lmdb_arckfs --benchmarks=$workload --compression=0 --use_existing_db=0 --threads=1 --batch=100 --num=$OP_COUNT > $output_dir/$FS/lmdb/$workload/Run$i 2>&1
        sudo rmmod sufs
    else
        numactl --membind=0 lmdb/dbbench/bin/t_lmdb --benchmarks=$workload --compression=0 --use_existing_db=0 --threads=1 --batch=100 --num=$OP_COUNT > $output_dir/$FS/lmdb/$workload/Run$i 2>&1

        sudo umount $PM_DEVICE 

        if [ $FS = "squirrelfs" ]; then 
            sudo rmmod squirrelfs
        elif [ $FS = "nova" ]; then 
            sudo rmmod nova 
        elif [ $FS = "winefs" ]; then
            sudo rmmod winefs
        fi
    fi
done
