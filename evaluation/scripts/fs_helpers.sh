#!/bin/bash

init_mount() {
    if [ $FS = "squirrelfs" ]; then 
        sudo mount -t squirrelfs -o init $PM_DEVICE $MOUNT_POINT/
    elif [ $FS = "nova" ]; then 
        sudo mount -t NOVA -o init $PM_DEVICE $MOUNT_POINT/
    elif [ $FS = "winefs" ]; then 
        sudo mount -t winefs -o init $PM_DEVICE $MOUNT_POINT/
    elif [ $FS = "ext4" ]; then 
        yes | sudo mkfs.ext4 $PM_DEVICE 
        sudo -E mount -t ext4 -o dax $PM_DEVICE $MOUNT_POINT/
    fi 
}

make_files_until_failure() {
    i=1
    touch $MOUNT_POINT/file${1}_0
    retval=$?
    while [ $retval -eq 0 ]
    do 
        touch $MOUNT_POINT/file${1}_$i >> /dev/null 2>&1
        retval=$?
        dd if=/dev/zero of=$MOUNT_POINT/file${1}_$i bs=$file_size count=1 status=none >> /dev/null 2>&1
        i=$((i+1))
    done
    echo "created $i files"
}

fill_device() {
    local thread=$1
    parent=$MOUNT_POINT/dir$thread
    mkdir -p $parent
    touch $parent/file_0
    retval=$?
    file_count=0
    for k in $(seq 31752) # fills up the device with 128 threads and 32KB files
    do 
        touch $parent/file_$k
        retval=$?
        if [ $retval -ne 0 ]
        then 
            echo "thread $thread ran out of space"
            return 
        fi
        dd if=/dev/zero of=$parent/file_$k bs=$fill_device_file_size count=1 status=none >> /dev/null 2>&1
        retval=$?
        if [ $retval -ne 0 ]
        then 
            echo "thread $thread ran out of space"
            echo "Thread $thread created $file_count files"
            return 
        else 
            file_count=$((file_count + 1))
        fi
    done
    df -h
    
}

remount() { 
    if [ $FS = "squirrelfs" ]; then 
        mount -t squirrelfs $PM_DEVICE $MOUNT_POINT/
    elif [ $FS = "nova" ]; then 
        mount -t NOVA $PM_DEVICE $MOUNT_POINT/
    elif [ $FS = "winefs" ]; then 
        mount -t winefs $PM_DEVICE $MOUNT_POINT/
    elif [ $FS = "ext4" ]; then 
        mount -t ext4 -o dax $PM_DEVICE $MOUNT_POINT/
    fi 
    
}

recovery_remount() {
    # measure squirrelfs recovery mount timing 
    if [ $FS = "squirrelfs" ]; then 
        filename=${filename}_recovery
        mkdir -p $filename
        mount -t squirrelfs -o force_recovery $PM_DEVICE $MOUNT_POINT/
    fi
}