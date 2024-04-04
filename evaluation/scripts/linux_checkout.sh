#!/bin/bash

FS=$1
device=/dev/pmem0
mount_point=/mnt/pmem
output_dir=output-25M/$FS/linux_checkout
iterations=10

versions=("v3.0" "v4.0" "v5.0" "v6.0")

if [ $FS = "squirrelfs" ]; then 
    sudo -E insmod $HOME/linux/fs/hayleyfs/hayleyfs.ko; sudo mount -t hayleyfs -o init $device $mount_point
elif [ $FS = "nova" ]; then 
    sudo -E insmod $HOME/linux/fs/nova/nova.ko; sudo mount -t NOVA -o init $device $mount_point
elif [ $FS = "winefs" ]; then 
    sudo -E insmod $HOME/linux/fs/winefs/winefs.ko; sudo mount -t winefs -o init $device $mount_point
elif [ $FS = "ext4" ]; then 
    yes | sudo mkfs.ext4 $device 
    sudo -E mount -t ext4 -o dax $device $mount_point
fi 

mkdir -p $output_dir
sudo chown -R $USER:$USER $output_dir
sudo mkdir -p $mount_point/linux 
sudo chown $USER:$USER $mount_point/linux
# check out the oldest tagged version
time git clone git@github.com:torvalds/linux.git $mount_point/linux --branch="v2.6.11"

test_dir=$(pwd)
cd $mount_point/linux

for i in $(seq $iterations)
do 
    for tag in ${versions[@]} 
    do 
        echo -n "$tag," >> $test_dir/$output_dir/Run$i
        TIMEFORMAT="%2R"; { time git checkout $tag; } 2>> $test_dir/$output_dir/Run$i 
    done
done 

cd $test_dir
sudo umount /dev/pmem0 -f 

if [ $FS = "squirrelfs" ]; then 
    sudo rmmod hayleyfs
elif [ $FS = "nova" ]; then 
    sudo rmmod nova 
elif [ $FS = "winefs" ]; then
    sudo rmmod winefs
fi