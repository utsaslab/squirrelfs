#!/bin/bash

FS=$1
workload=$2
output_dir="output-ae"
iterations=5

op_count=100000000

mkdir -p $output_dir/$FS/lmdb/$workload

if [ $FS = "arckfs" ]; then 
    sudo ndctl create-namespace -f -e namespace0.0 --mode=devdax
else 
    sudo ndctl create-namespace -f -e namespace0.0 --mode=fsdax
fi

for i in $(seq $iterations)
do
    if [ $FS = "squirrelfs" ]; then 
        sudo -E insmod ../linux/fs/squirrelfs/squirrelfs.ko; sudo mount -t squirrelfs -o init /dev/pmem0 /mnt/pmem/
    elif [ $FS = "nova" ]; then 
        sudo -E insmod ../linux/fs/nova/nova.ko; sudo mount -t NOVA -o init /dev/pmem0 /mnt/pmem/
    elif [ $FS = "winefs" ]; then 
        sudo -E insmod ../linux/fs/winefs/winefs.ko; sudo mount -t winefs -o init /dev/pmem0 /mnt/pmem/
    elif [ $FS = "ext4" ]; then 
        yes | sudo mkfs.ext4 /dev/pmem0 
        sudo -E mount -t ext4 -o dax /dev/pmem0 /mnt/pmem/
    elif [ $FS = "arckfs" ]; then 
        sudo insmod arckfs/kfs/sufs.ko pm_nr=1
        sudo chmod 666 /dev/supremefs
        sudo arckfs/fsutils/init
    fi 

    

    if [ $FS = "arckfs" ]; then 
        numactl --membind=0 lmdb/dbbench/bin/t_lmdb_arckfs --benchmarks=$2 --compression=0 --use_existing_db=0 --threads=1 --batch=100 --num=$op_count > $output_dir/$FS/lmdb/$workload/Run$i 2>&1
        sudo rmmod sufs
    else
        numactl --membind=0 lmdb/dbbench/bin/t_lmdb --benchmarks=$2 --compression=0 --use_existing_db=0 --threads=1 --batch=100 --num=$op_count > $output_dir/$FS/lmdb/$workload/Run$i 2>&1

        sudo umount /dev/pmem0 

        if [ $FS = "squirrelfs" ]; then 
            sudo rmmod squirrelfs
        elif [ $FS = "nova" ]; then 
            sudo rmmod nova 
        elif [ $FS = "winefs" ]; then
            sudo rmmod winefs
        fi
    fi
done