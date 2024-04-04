#!/bin/bash

sudo mount -t hayleyfs -o init /dev/pmem0 /mnt/pmem
sudo touch /mnt/pmem/foo
sudo touch /mnt/pmem/bar
sudo mv /mnt/pmem/foo /mnt/pmem/bar

sudo umount /dev/pmem0
sudo mount -t hayleyfs /dev/pmem0 /mnt/pmem

stat /mnt/pmem/foo
stat /mnt/pmem/bar