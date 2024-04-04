#!/bin/bash

gcc io_size_test.c -o io_size_test

echo "io size,file_system,avg write latency(us),avg read latency(us),write throughput(kb/ms),read throughput(kb/ms)" > io_size_output.csv

run_rust() {
    echo -n "rust$2,$1," >> io_size_output.csv
    sudo insmod ~/linux/fs/hayleyfs/hayleyfs.ko 
    sudo mount -t hayleyfs -o init,write_type=$2 /dev/pmem0 /mnt/pmem
    sudo numactl --membind 0 ./io_size_test $1 >> io_size_output.csv
    sudo umount /dev/pmem0 
    sudo rmmod hayleyfs 
}

run_nova() {
    echo -n "nova,$1," >> io_size_output.csv
    sudo insmod ~/linux/fs/nova/nova.ko 
    sudo mount -t NOVA -o init /dev/pmem0 /mnt/pmem
    sudo numactl --membind 0 ./io_size_test $1 >> io_size_output.csv
    sudo umount /dev/pmem0 
    sudo rmmod nova 
}

run_ext4() {
    echo -n "ext4$2,$1," >> io_size_output.csv
    echo "yes" | sudo mkfs.ext4 /dev/pmem0 
    sudo numactl --membind 0 mount -t ext4 -o dax,data=$2 /dev/pmem0 /mnt/pmem/ 
    sudo ./io_size_test $1 >> io_size_output.csv
    sudo umount /dev/pmem0 
}

run_rust $((1024*4)) 0
run_rust $((1024*8)) 0
run_rust $((1024*16)) 0
run_rust $((1024*64)) 0
run_rust $((1024*256)) 0
run_rust $((1024*512)) 0
run_rust $((1024*1024)) 0 
run_rust $((1024*1024*2)) 0
run_rust $((1024*1024*4)) 0

run_rust $((1024*4)) 1
run_rust $((1024*8)) 1
run_rust $((1024*16)) 1
run_rust $((1024*64)) 1
run_rust $((1024*256)) 1
run_rust $((1024*512)) 1
run_rust $((1024*1024)) 1 
run_rust $((1024*1024*2)) 1
run_rust $((1024*1024*4)) 1

run_rust $((1024*4)) 2
run_rust $((1024*8)) 2
run_rust $((1024*16)) 2
run_rust $((1024*64)) 2
run_rust $((1024*256)) 2
run_rust $((1024*512)) 2
run_rust $((1024*1024)) 2 
run_rust $((1024*1024*2)) 2
run_rust $((1024*1024*4)) 2

run_nova $((1024*4)) 
run_nova $((1024*8)) 
run_nova $((1024*16)) 
run_nova $((1024*64)) 
run_nova $((1024*256)) 
run_nova $((1024*512)) 
run_nova $((1024*1024)) 
run_nova $((1024*1024*2)) 
run_nova $((1024*1024*4)) 

run_ext4 $((1024*4)) ordered
run_ext4 $((1024*8)) ordered
run_ext4 $((1024*16)) ordered
run_ext4 $((1024*64)) ordered
run_ext4 $((1024*256)) ordered
run_ext4 $((1024*512)) ordered
run_ext4 $((1024*1024)) ordered
run_ext4 $((1024*1024*2)) ordered
run_ext4 $((1024*1024*4)) ordered

run_ext4 $((1024*4)) writeback
run_ext4 $((1024*8)) writeback
run_ext4 $((1024*16)) writeback
run_ext4 $((1024*64)) writeback
run_ext4 $((1024*256)) writeback
run_ext4 $((1024*512)) writeback
run_ext4 $((1024*1024)) writeback
run_ext4 $((1024*1024*2)) writeback
run_ext4 $((1024*1024*4)) writeback
