#!/bin/bash
FS=$1
MOUNT_POINT=$2
TEST=$3
OUTPUT_DIR=$4
PM_DEVICE=$5
ITERATIONS=$6

if [ -z $FS ] | [ -z $MOUNT_POINT ] | [ -z $TEST ] | [ -z $OUTPUT_DIR ] | [ -z $PM_DEVICE ] | [ -z $ITERATIONS ]; then 
    echo "Usage: remount_timing.sh fs mountpoint test output_dir pm_device iterations"
    exit 1
fi
sudo mkdir -p $MOUNT_POINT
sudo mkdir -p $OUTPUT_DIR

# iterations=10
dirs=32
files=1000
file_size=$((1024*1024))
fill_device_file_size=$((1024*32))
filename=$OUTPUT_DIR/${FS}/remount_timing/$TEST
mkdir -p $filename

trap ctrl_c INT 

function ctrl_c() {
    kill -TERM $(jobs -p) 2>/dev/null || true
}

time_mount() { 
    # measure regular mount timing
    for i in $(seq $ITERATIONS)
    do
        if [ $FS = "squirrelfs" ]; then 
            { time mount -t squirrelfs $PM_DEVICE $MOUNT_POINT/; } > $filename/Run$i 2>&1
        elif [ $FS = "nova" ]; then 
            { time mount -t NOVA $PM_DEVICE $MOUNT_POINT/; } > $filename/Run$i 2>&1
        elif [ $FS = "winefs" ]; then 
            { time mount -t winefs $PM_DEVICE $MOUNT_POINT/; } > $filename/Run$i 2>&1
        elif [ $FS = "ext4" ]; then 
            { time mount -t ext4 -o dax $PM_DEVICE $MOUNT_POINT/; } > $filename/Run$i 2>&1
        fi 
        sudo umount $PM_DEVICE
    done
    # measure squirrelfs recovery mount timing 
    if [ $FS = "squirrelfs" ]; then 
        filename=${filename}_recovery
        mkdir -p $filename
        for i in $(seq $ITERATIONS)
        do
            { time mount -t squirrelfs -o force_recovery $PM_DEVICE $MOUNT_POINT/; } > $filename/Run$i 2>&1
            sudo umount $PM_DEVICE
        done
    fi
}

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
        touch $MOUNT_POINT/file${1}_$i
        retval=$?
        dd if=/dev/zero of=$MOUNT_POINT/file${1}_$i bs=$file_size count=1 status=none
        i=$((i+1))
    done
    echo "created $i files"
}

# 128GB fills up with approximately 2k files per thread
make_files_half_full() {
    touch $MOUNT_POINT/file${1}_0
    retval=$?
    for i in $(seq 1000)
    do 
        touch $MOUNT_POINT/file${1}_$i
        retval=$?
        dd if=/dev/zero of=$MOUNT_POINT/file${1}_$i bs=$file_size count=1 status=none
    done
}

make_dirs_until_failure() {
    local thread=$1
    local parent=$2
    local total_dirs=1
    local queue=($parent/dir${thread}_0)

    # make top level directories 
    mkdir $parent/dir${thread}_0
    retval=$?
    if [ $retval -ne 0 ]
    then 
        return 
    fi
    for i in $(seq 1 $dirs)
    do
        mkdir $parent/dir${thread}_$i
        retval=$?

        total_dirs=$((total_dirs+1))
        if [ $retval -ne 0 ]
        then 
            echo "total dirs $total_dirs"
            return 
        fi
        queue+=($parent/dir${thread}_$i)
    done 
    
    # then work through the queue and create dirs in each directory
    while [ ${#queue[@]} -gt 0 ]
    do 
        path=${queue[0]}
        queue=("${queue[@]:1}")

        for i in $(seq $dirs)
        do 
            mkdir $path/dir${thread}_$i
            retval=$?
            total_dirs=$((total_dirs+1))
            if [ $retval -ne 0 ]
            then 
                echo "total dirs $total_dirs"
                return 
            fi
            queue+=($path/dir${thread}_$i)
        done
    done
}

make_dirs_half_full() {
    local thread=$1
    local parent=$2
    local total_dirs=1
    local queue=($parent/dir${thread}_0)

    # make top level directories 
    mkdir $parent/dir${thread}_0
    retval=$?
    if [ $retval -ne 0 ]
    then 
        return 
    fi
    for i in $(seq 1 $dirs)
    do
        mkdir $parent/dir${thread}_$i
        retval=$?

        total_dirs=$((total_dirs+1))
        if [ $retval -ne 0 ]
        then 
            echo "total dirs $total_dirs"
            return 
        fi
        queue+=($parent/dir${thread}_$i)
    done 
    
    # then work through the queue and create dirs in each directory
    while [ ${#queue[@]} -gt 0 ]
    do 
        path=${queue[0]}
        queue=("${queue[@]:1}")

        for i in $(seq $dirs)
        do 
            mkdir $path/dir${thread}_$i
            retval=$?
            total_dirs=$((total_dirs+1))
            if [ $retval -ne 0 ]
            then 
                echo "total dirs $total_dirs"
                return 
            elif [ $total_dirs -eq 30000 ]
            then 
                return 
            fi
            queue+=($path/dir${thread}_$i)
        done
    done
}

fill_device() {
    local thread=$1
    parent=$MOUNT_POINT/dir$thread
    mkdir -p $parent
    touch $parent/file_0
    retval=$?
    for i in $(seq 31752) # fills up the device with 128 threads and 32KB files
    do 
        touch $parent/file_$i
        retval=$?
        if [ $retval -ne 0 ]
        then 
            echo "thread $thread ran out of space"
            return 
        fi
        dd if=/dev/zero of=$parent/file_$i bs=$fill_device_file_size count=1 status=none
        retval=$?
        if [ $retval -ne 0 ]
        then 
            echo "thread $thread ran out of space"
            return 
        fi
    done
}

if [ $FS = "squirrelfs" ]; then 
    sudo -E insmod ../linux/fs/squirrelfs/squirrelfs.ko
    retval=$?
    if [ $retval != 0 ]; then 
        echo "Exiting, error code $?"
        exit 1
    fi 
elif [ $FS = "nova" ]; then 
    sudo -E insmod ../linux/fs/nova/nova.ko
    retval=$?
    if [ $retval != 0 ]; then 
        echo "Exiting, error code $?"
        exit 1
    fi 
elif [ $FS = "winefs" ]; then 
    sudo -E insmod ../linux/fs/winefs/winefs.ko
    retval=$?
    if [ $retval != 0 ]; then 
        echo "Exiting, error code $?"
        exit 1
    fi 
fi 

echo $TEST

# set up files to scan on remount
if [[ $TEST == "files" ]]
then 
    init_mount
    # just create a bunch of files and fill them in
    for i in $(seq $dirs)
    do 
        mkdir $MOUNT_POINT/dir$i
        for j in $(seq $files)
        do 
            touch $MOUNT_POINT/dir$i/file$j
            dd if=/dev/zero of=$MOUNT_POINT/dir$i/file$j bs=$file_size count=1 status=none
        done
    done
    df -h 
    sudo umount $PM_DEVICE
    time_mount
elif [[ $TEST == "dirs" ]]
then 
    init_mount
    # create a bunch of directories
    for i in $(seq $dirs)
    do 
        for j in $(seq $dirs)
        do 
            for k in $(seq $dirs)
            do 
                mkdir -p $MOUNT_POINT/dir$i/dir$j/dir$k
            done 
        done 
    done 
    df -h 
    sudo umount $PM_DEVICE
    time_mount
elif [[ $TEST == "init" ]]
then 
    for i in $(seq $ITERATIONS)
        do 
        if [ $FS = "squirrelfs" ]; then 
            { time sudo mount -t squirrelfs -o init $PM_DEVICE $MOUNT_POINT/; } > $filename/Run$i 2>&1
        elif [ $FS = "nova" ]; then 
            { time sudo mount -t NOVA -o init $PM_DEVICE $MOUNT_POINT/; } > $filename/Run$i 2>&1
        elif [ $FS = "winefs" ]; then 
            { time sudo mount -t winefs -o init $PM_DEVICE $MOUNT_POINT/; } > $filename/Run$i 2>&1
        elif [ $FS = "ext4" ]; then 
            { time sh -c "yes | sudo mkfs.ext4 $PM_DEVICE; sudo -E mount -t ext4 -o dax $PM_DEVICE $MOUNT_POINT/";} > $filename/Run$i 2>&1
        fi 
        sudo umount $PM_DEVICE
    done 
elif [[ $TEST == "empty" ]]
then 
    init_mount 
    sudo umount $PM_DEVICE
    time_mount
elif [[ $TEST == "fill_files" ]]
then
    init_mount 
    # fill file system with empty files 
    for i in $(seq 63)
    do 
        make_files_until_failure $i &
    done 
    make_files_until_failure 64
    sleep 30
    sudo umount $PM_DEVICE
    time_mount
elif [[ $TEST == "half_files" ]]
then 
    init_mount 
    for i in $(seq 63)
    do 
        make_files_half_full $i &
    done 
    make_files_half_full 64 
    sleep 30
    df -h
    sudo umount $PM_DEVICE
    time_mount
elif [[ $TEST == "fill_dirs" ]]
then 
    init_mount 
    for i in $(seq 63)
    do 
        mkdir $MOUNT_POINT/dir$i
        make_dirs_until_failure $i $MOUNT_POINT/dir$i &
    done 
    mkdir $MOUNT_POINT/dir64
    make_dirs_until_failure 64 $MOUNT_POINT/dir64
    sleep 30
    df -i
    sudo umount $PM_DEVICE
    time_mount
elif [[ $TEST == "half_dirs" ]]
then 
    init_mount 
    for i in $(seq 63)
    do 
        mkdir $MOUNT_POINT/dir$i
        make_dirs_half_full $i $MOUNT_POINT/dir$i &
    done 
    mkdir $MOUNT_POINT/dir64
    make_dirs_half_full 64 $MOUNT_POINT/dir64
    sleep 30
    df -i
    sudo umount $PM_DEVICE
    time_mount
elif [[ $TEST == "fill_device" ]]
then 
    init_mount 
    for i in $(seq 127)
    do 
        fill_device $i &
    done 
    fill_device 128 
    sleep 30
    df -h
    df -i
    sudo umount $PM_DEVICE
    time_mount
fi 
echo "done running tests"

if [ $FS = "squirrelfs" ]; then 
    echo "removing module"
    sudo rmmod squirrelfs
elif [ $FS = "nova" ]; then 
    sudo rmmod nova 
elif [ $FS = "winefs" ]; then
    sudo rmmod winefs
fi