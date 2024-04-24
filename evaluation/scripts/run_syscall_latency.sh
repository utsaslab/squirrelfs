#!/bin/bash 

FS=$1
MOUNT_POINT=$2
OUTPUT_DIR=$3
PM_DEVICE=$4
output_dir=$OUTPUT_DIR

if [ -z $FS ] || [ -z $MOUNT_POINT ] || [ -z $OUTPUT_DIR ] || [ -z $PM_DEVICE ]; then 
    echo "Usage: run_syscall_latency.sh fs mountpoint output_dir pm_device"
    exit 1
fi
sudo mkdir -p $MOUNT_POINT
sudo mkdir -p $OUTPUT_DIR

test_iterations=10

# program will load module and mount arckfs itself
if [ $FS = "squirrelfs" ]; then 
    sudo -E insmod ../linux/fs/squirrelfs/squirrelfs.ko
elif [ $FS = "nova" ]; then 
    sudo -E insmod ../linux/fs/nova/nova.ko
elif [ $FS = "winefs" ]; then 
    sudo -E insmod ../linux/fs/winefs/winefs.ko
fi 

if [ $FS = "arckfs" ]; then 
    (cd arckfs && ./compile.sh)
    (cd tests && make)
else 
    (cd tests && gcc syscall_latency.c -o syscall_latency)
fi

for i in mkdir creat append_1k append_16k read_1k read_16k unlink rename
do 
    mkdir -p $output_dir/$FS/syscall_latency/$i
done

if [ $FS = "arckfs" ]; then 
    # arckfs must be set up already when we run the program, so we'll call it in a loop
    sudo ndctl create-namespace -f -e namespace0.0 --mode=devdax
    # TODO: should these be in the loop?
    sudo insmod arckfs/kfs/sufs.ko pm_nr=1
    sudo chmod 666 /dev/supremefs
    sudo arckfs/fsutils/init
    for syscall in mkdir creat append_1k append_16k read_1k read_16k unlink rename
    do 
        echo $syscall
        for i in $(seq 0 $(($test_iterations - 1)))
        do 
            # numactl --cpunodebind=1 --membind=1 tests/syscall_latency_arckfs $syscall $i
            numactl tests/syscall_latency_arckfs $syscall $i
            sudo rmmod sufs
            sudo insmod arckfs/kfs/sufs.ko pm_nr=1
            sudo chmod 666 /dev/supremefs
            sudo arckfs/fsutils/init
        done 
    done
else 
    sudo umount $PM_DEVICE >> /dev/null 2>&1
    sudo ndctl create-namespace -f -e namespace0.0 --mode=fsdax
    tests/syscall_latency $FS $MOUNT_POINT $OUTPUT_DIR $PM_DEVICE
fi

sudo umount $PM_DEVICE >> /dev/null 2>&1
if [ $FS = "squirrelfs" ]; then 
    sudo rmmod squirrelfs
elif [ $FS = "nova" ]; then 
    sudo rmmod nova 
elif [ $FS = "winefs" ]; then
    sudo rmmod winefs
elif [ $FS = "arckfs" ]; then 
    sudo rmmod sufs
fi
