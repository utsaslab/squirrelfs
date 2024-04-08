#!/bin/bash
sudo umount /dev/pmem0
sudo rmmod hayleyfs
make LLVM=-14 fs/hayleyfs/hayleyfs.ko
sudo insmod fs/hayleyfs/hayleyfs.ko
sudo mount -o init -t hayleyfs /dev/pmem0 /mnt/pmem
touch test.txt
echo "Hello World" > test.txt
sudo mv test.txt /mnt/pmem
rm test.txt
cd /mnt/pmem
pwd
