#!/bin/bash

FS=$1
PROCESSES=$2
output=ycsb_repro_traces


if [ $FS = "squirrelfs" ]; then 
    insmod /mnt/local_ssd/home/hayley/linux/fs/squirrelfs/squirrelfs.ko; sudo mount -t squirrelfs -o init /dev/pmem0 /mnt/pmem/
elif [ $FS = "nova" ]; then 
    sudo -E insmod /mnt/local_ssd/home/hayley/linux/fs/nova/nova.ko; sudo mount -t NOVA -o init /dev/pmem0 /mnt/pmem/
elif [ $FS = "winefs" ]; then 
    sudo -E insmod /mnt/local_ssd/home/hayley/linux/fs/winefs/winefs.ko; sudo mount -t winefs -o init /dev/pmem0 /mnt/pmem/
elif [ $FS = "ext4" ]; then 
    yes | sudo mkfs.ext4 /dev/pmem0 
    sudo -E mount -t ext4 -o dax /dev/pmem0 /mnt/pmem/
fi 

gcc tests/ycsb_loada_repro.c -o ycsb_loada_repro -g
gcc tests/ycsb_runa_trace.c -o ycsb_runa_trace -g
gcc tests/ycsb_rune_trace.c -o ycsb_rune_trace -g
mkdir -p $output

# for i in $(seq $(($PROCESSES-1)))
# do 
# sudo ./ycsb_loada_repro 1000000 &
# done 

# sudo ../magic-trace run -trace-include-kernel -o $output/${FS}_load_$PROCESSES ./ycsb_loada_repro -- 500000

# while ! sudo umount /dev/pmem0 
# do 
#     sleep 5
# done

# if [ $FS = "squirrelfs" ]; then 
#     sudo mount -t squirrelfs /dev/pmem0 /mnt/pmem/
# elif [ $FS = "nova" ]; then 
#     sudo mount -t NOVA /dev/pmem0 /mnt/pmem/
# elif [ $FS = "winefs" ]; then 
#     sudo mount -t winefs /dev/pmem0 /mnt/pmem/
# elif [ $FS = "ext4" ]; then 
#     sudo -E mount -t ext4 -o dax /dev/pmem0 /mnt/pmem/
# fi 

# for i in $(seq $(($PROCESSES-1)))
# do 
# sudo ./ycsb_runa_trace 1000000 &
# done 

# sudo ../magic-trace run -trace-include-kernel -o $output/${FS}_runa_$PROCESSES ./ycsb_runa_trace -- 500000

# while ! sudo umount /dev/pmem0 
# do 
#     sleep 5
# done

for i in $(seq $(($PROCESSES-1)))
do 
sudo ./ycsb_rune_trace 10000 &
done 

sudo ../magic-trace run -trace-include-kernel -o $output/${FS}_rune_$PROCESSES ./ycsb_rune_trace -- 50000

while ! sudo umount /dev/pmem0 
do 
    sleep 5
done

if [ $FS = "squirrelfs" ]; then 
    sudo rmmod squirrelfs
elif [ $FS = "nova" ]; then 
    sudo rmmod nova 
elif [ $FS = "winefs" ]; then
    sudo rmmod winefs
fi