#!/bin/bash

key_path=$HOME/.ssh/id_ed25519

FS=$1
mount_point=$2
output_dir=$3
pm_device=$4
ITERATIONS=$5

if [ -z $FS ] | [ -z $mount_point ] | [ -z $output_dir ] | [ -z $pm_device ] | [ -z $ITERATIONS ]; then 
    echo "Usage: linux_checkout.sh fs mountpoint output_dir pm_device iterations"
    exit 1
fi
sudo mkdir -p $mount_point
sudo mkdir -p $output_dir

# iterations=10

versions=("v3.0" "v4.0" "v5.0" "v6.0")

if [ $FS = "squirrelfs" ]; then 
    sudo -E insmod ../linux/fs/squirrelfs/squirrelfs.ko; sudo mount -t squirrelfs -o init $pm_device $mount_point
elif [ $FS = "nova" ]; then 
    sudo -E insmod ../linux/fs/nova/nova.ko; sudo mount -t NOVA -o init $pm_device $mount_point
elif [ $FS = "winefs" ]; then 
    sudo -E insmod ../linux/fs/winefs/winefs.ko; sudo mount -t winefs -o init $pm_device $mount_point
elif [ $FS = "ext4" ]; then 
    yes | sudo mkfs.ext4 $pm_device 
    sudo -E mount -t ext4 -o dax $pm_device $mount_point
fi 

mkdir -p $output_dir
sudo chown -R $USER:$USER $output_dir
sudo mkdir -p $mount_point/linux 
sudo chown $USER:$USER $mount_point/linux

time GIT_SSH_COMMAND="ssh -i ${key_path} -o IdentitiesOnly=yes" git clone git@github.com:torvalds/linux.git $mount_point/linux #--branch="v2.6.11"
# time git clone git@github.com:torvalds/linux.git $mount_point/linux --branch="v2.6.11"

test_dir=$(pwd)
cd $mount_point/linux
# check out the oldest tagged version
GIT_SSH_COMMAND='ssh -i  -o IdentitiesOnly=yes' git checkout v2.6.12

for i in $(seq $ITERATIONS)
do 
    for tag in ${versions[@]} 
    do 
        echo -n "$tag," >> $test_dir/$output_dir/$FS/checkout/Run$i
        TIMEFORMAT="%2R"; { time GIT_SSH_COMMAND="ssh -i ${key_path} -o IdentitiesOnly=yes" git checkout $tag; } 2>> $test_dir/$output_dir/$FS/checkout/Run$i 
    done
done 

cd $test_dir
sudo umount $pm_device -f 

if [ $FS = "squirrelfs" ]; then 
    sudo rmmod squirrelfs
elif [ $FS = "nova" ]; then 
    sudo rmmod nova 
elif [ $FS = "winefs" ]; then
    sudo rmmod winefs
fi