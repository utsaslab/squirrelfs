#!/bin/bash

FS=$1
output_dir=memory_usage_output/$FS
iterations=2

mkdir -p $output_dir

for iter in $(seq $iterations)
do 
    if [ $FS = "squirrelfs" ]; then 
        insmod $HOME/linux/fs/hayleyfs/hayleyfs.ko; sudo mount -t hayleyfs -o init /dev/pmem0 /mnt/pmem/
    elif [ $FS = "nova" ]; then 
        insmod $HOME/linux/fs/nova/nova.ko; sudo mount -t NOVA -o init /dev/pmem0 /mnt/pmem/
    elif [ $FS = "winefs" ]; then 
        insmod $HOME/linux/fs/winefs/winefs.ko; sudo mount -t winefs -o init /dev/pmem0 /mnt/pmem/
    elif [ $FS = "ext4" ]; then 
        yes | sudo mkfs.ext4 /dev/pmem0 
        mount -t ext4 -o dax /dev/pmem0 /mnt/pmem/
    fi 

    cat /proc/meminfo > $output_dir/pre_usage$iter

    for i in {1..100}
    do 
        dir=/mnt/pmem/dir$i
        mkdir -p $dir
        for j in {1..100}
        do 
            touch $dir/file$j
            dd if=/dev/zero of=$dir/file$j bs=16384 count=1 > /dev/null 2>&1
        done 
    done

    cat /proc/meminfo > $output_dir/post_usage$iter

    umount /dev/pmem0

    # remount and check memory usage immediately after remount

    if [ $FS = "squirrelfs" ]; then 
        mount -t hayleyfs /dev/pmem0 /mnt/pmem/
    elif [ $FS = "nova" ]; then 
        mount -t NOVA /dev/pmem0 /mnt/pmem/
    elif [ $FS = "winefs" ]; then 
        mount -t winefs /dev/pmem0 /mnt/pmem/
    elif [ $FS = "ext4" ]; then 
        mount -t ext4 -o dax /dev/pmem0 /mnt/pmem/
    fi 

    cat /proc/meminfo > $output_dir/remount_usage$iter

    umount /dev/pmem0

    if [ $FS = "squirrelfs" ]; then 
        sudo rmmod hayleyfs
    elif [ $FS = "nova" ]; then 
        sudo rmmod nova 
    elif [ $FS = "winefs" ]; then
        sudo rmmod winefs
    fi

    pre_usage=`cat $output_dir/pre_usage$iter | awk '/MemFree/ {print $2}'`
    echo $pre_usage
    post_usage=`cat $output_dir/post_usage$iter | awk '/MemFree/ {print $2}'`
    echo $post_usage
    kb_used=$((pre_usage - post_usage))
    mb_used=$(bc <<< "scale=2;$kb_used / 1024")
    echo "$mb_used MB used"

    remount_usage=`cat $output_dir/remount_usage$iter | awk '/MemFree/ {print $2}'`
    echo $remount_usage 
    kb_used=$((pre_usage - remount_usage))
    mb_used=$(bc <<< "scale=2;$kb_used / 1024")
    echo "$mb_used MB used after remount"
done 

