#!/bin/bash 

FS=$1
MOUNT_POINT=$2
TEST=$3
OUTPUT_DIR=$4
PM_DEVICE=$5
ITERATIONS=$6

if [ -z $FS ] || [ -z $MOUNT_POINT ] || [ -z $TEST ] || [ -z $OUTPUT_DIR ] || [ -z $PM_DEVICE ] || [ -z $ITERATIONS ]; then
    echo "Usage: remount_timing.sh fs mountpoint test output_dir pm_device iterations"
    exit 1
fi
sudo mkdir -p $MOUNT_POINT
sudo mkdir -p $OUTPUT_DIR

dirs=32
files=1000
file_size=$((1024*1024))
fill_device_file_size=$((1024*32))
filename=$OUTPUT_DIR/${FS}/memory_usage/$TEST
rm -rf $filename
mkdir -p $filename

trap ctrl_c INT 

function ctrl_c() {
    kill -TERM $(jobs -p) 2>/dev/null || true
}

. scripts/fs_helpers.sh

setup() {
    if [ $FS = "squirrelfs" ]; then 
        sudo -E insmod ../linux/fs/squirrelfs/squirrelfs.ko
        retval=$?
        if [ $retval != 0 ]; then 
            echo "Exiting, error code $retval"
            exit 1
        fi 
    elif [ $FS = "nova" ]; then 
        sudo -E insmod ../linux/fs/nova/nova.ko
        retval=$?
        if [ $retval != 0 ]; then 
            echo "Exiting, error code $retval"
            exit 1
        fi 
    elif [ $FS = "winefs" ]; then 
        sudo -E insmod ../linux/fs/winefs/winefs.ko
        retval=$?
        if [ $retval != 0 ]; then 
            echo "Exiting, error code $retval"
            exit 1
        fi 
    fi 
}

cleanup() {
    sudo umount /dev/pmem0 > /dev/null 2>&1
    sleep 2
    if [ $FS = "squirrelfs" ]; then 
        echo "removing module"
        sudo rmmod squirrelfs
    elif [ $FS = "nova" ]; then 
        sudo rmmod nova 
    elif [ $FS = "winefs" ]; then
        sudo rmmod winefs
    fi
}

init_test() {
    echo "$FS init"
    for i in $(seq $ITERATIONS); do 
        echo -n "Before: " >> $filename/Run$i
        cat /proc/meminfo | grep MemAvailable >> $filename/Run$i
        init_mount
        echo -n "After: " >> $filename/Run$i
        cat /proc/meminfo | grep MemAvailable >> $filename/Run$i
        sudo umount /dev/pmem0
    done
}

empty_test() {
    echo "$FS empty"
    for i in $(seq $ITERATIONS); do 
        init_mount 
        sudo umount /dev/pmem0
        echo -n "Before: " >> $filename/Run$i
        cat /proc/meminfo | grep MemAvailable >> $filename/Run$i
        remount
        echo -n "After: " >> $filename/Run$i
        cat /proc/meminfo | grep MemAvailable >> $filename/Run$i
        sudo umount /dev/pmem0
    done
}

empty_recovery_test() {
    echo "$FS empty recovery"
    if [ $FS = "squirrelfs" ]
    then 
        for i in $(seq $ITERATIONS); do 
            init_mount 
            sudo umount /dev/pmem0
            echo -n "Before: " >> $filename/Run$i
            cat /proc/meminfo | grep MemAvailable >> $filename/Run$i
            recovery_remount
            echo -n "After: " >> $filename/Run$i
            cat /proc/meminfo | grep MemAvailable >> $filename/Run$i
            sudo umount /dev/pmem0
        done
    else 
        echo "Recovery tests are only supported with SquirrelFS"
    fi 
}

full_test() {
    echo "$FS full"
    for i in $(seq $ITERATIONS); do 
        init_mount
        echo -n "Before: " >> $filename/Run$i 
        cat /proc/meminfo | grep MemAvailable >> $filename/Run$i
        for j in $(seq 127)
        do 
            fill_device $j &
        done 
        fill_device 128 
        sleep 30
        df -h
        df -i
        echo -n "After: " >> $filename/Run$i
        cat /proc/meminfo | grep MemAvailable >> $filename/Run$i
        sudo umount /dev/pmem0
    done
}

full_test2() {
    echo "$FS full"
    for i in $(seq $ITERATIONS); do 
        init_mount
        sleep 5
        echo -n "Before: " >> $filename/Run$i 
        cat /proc/meminfo | grep MemAvailable >> $filename/Run$i
        for j in $(seq 7)
        do 
            fill_device $j &
        done 
        fill_device 8 
        # sleep 30
        df -h
        df -i
        echo -n "After: " >> $filename/Run$i
        cat /proc/meminfo | grep MemAvailable >> $filename/Run$i
        sudo umount /dev/pmem0
        echo -n "Before 2: " >> $filename/Run$i 
        sleep 5
        if [ $FS = "ext4" ]; then
            sudo mount -t $FS -o dax /dev/pmem0 /mnt/pmem
        elif [ $FS = "nova" ]; then 
            sudo rmmod $FS 
            sleep 5
            sudo -E insmod ../linux/fs/$FS/$FS.ko
            sudo mount -t NOVA /dev/pmem0 /mnt/pmem
        else 
            sudo rmmod $FS 
            sleep 5
            sudo -E insmod ../linux/fs/$FS/$FS.ko
            sudo mount -t $FS /dev/pmem0 /mnt/pmem
        fi
        sleep 5
        echo -n "After 2: " >> $filename/Run$i
    done
}

full_recovery_test() {
    echo "$FS recovery"
    if [ $FS = "squirrelfs" ]
    then 
        for i in $(seq $ITERATIONS); do 
            init_mount
            fill_device
            cleanup
            echo -n "Before: " >> $filename/Run$i
            cat /proc/meminfo | grep MemAvailable >> $filename/Run$i
            recovery_remount
            echo -n "After: " >> $filename/Run$i
            cat /proc/meminfo | grep MemAvailable >> $filename/Run$i
            cleanup
        done
    else 
        echo "Recovery tests are only supported with SquirrelFS"
    fi 
}

main() {
    setup

    if [[ $TEST == "init" ]]
    then 
        init_test
    elif [[ $TEST == "empty" ]]
    then 
        empty_test
    elif [[ $TEST == "full" ]]
    then
        full_test
    elif [[ $TEST == "empty_recovery" ]]
    then
        empty_recovery_test
    elif [[ $TEST == "full_recovery" ]]
    then 
        full_recovery_test
    else 
        echo "Unrecognized test type $TEST"
    fi

    cleanup
}

main