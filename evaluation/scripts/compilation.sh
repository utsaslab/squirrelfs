#!/bin/bash

FS=$1
OUTPUT_DIR=$2
iterations=$3

eval_dir=$(pwd)
KERNEL_SOURCE=../../linux

output_dir=$OUTPUT_DIR/$FS/compilation
sudo mkdir -p $output_dir
sudo chown -R "${SUDO_USER:-$USER}":"${SUDO_USER:-$USER}" $output_dir
if [ $FS = "nova" ] || [ $FS = "squirrelfs" ] || [ $FS = "winefs" ]
then
    cd ../linux/fs/$FS 
elif [ $FS = "ext4" ]
then 
    cd ext4
fi

for i in $(seq $iterations)
do 
    if [ $FS = "nova" ] || [ $FS = "squirrelfs" ] || [ $FS = "winefs" ]
    then
        rm *.ko *.o *.mod *.mod.c
        cd ../..
        TIMEFORMAT="%2R"; { time make LLVM=-14 fs/$FS/$FS.ko; } 2> $eval_dir/$output_dir/Run$i
        cd fs/$FS
    elif [ $FS = "ext4" ]
    then 
        rm *.o .*.o.cmd 
        TIMEFORMAT="%2R"; { time make LLVM=-14 -C ${KERNEL_SOURCE} M=${PWD} modules; } 2> $eval_dir/$output_dir/Run$i
    fi
done

# make sure the file systems are all compiled at the end
if [ $FS = "nova" ] || [ $FS = "squirrelfs" ] || [ $FS = "winefs" ]
then
    cd ../..
    make LLVM=-14 fs/$FS/$FS.ko
fi

cd $eval_dir