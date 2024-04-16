#!/bin/bash

# note: download the 0.17.0 release of rocksdb for this. cloning the version on github has weird errors 

FS=$1
output_dir="output-ae"
numthreads=8
iterations=5

operation_count=25000000
record_count=25000000
echo $FS

ulimit -n 32768
ulimit -c unlimited

load_workload() {
    workload_id=$1
    i=$2

    cat /proc/vmstat | grep -e "pgfault" -e "pgmajfault" -e "thp" -e "nr_file" > $output_dir/$FS/rocksdb/Load$workload_id/pg_faults_before_Run$i 2>&1

    echo "load $workload_id"
    time sudo -E numactl --membind=0 ycsb-0.17.0/bin/ycsb load rocksdb -threads $numthreads -P "$HOME/ycsb-0.17.0/workloads/workload$workload_id" -p rocksdb.dir=/mnt/pmem/rocksdb -p recordcount=$record_count -p operationcount=$operation_count > $output_dir/$FS/rocksdb/Load$workload_id/Run$i 2>&1

    cat /proc/vmstat | grep -e "pgfault" -e "pgmajfault" -e "thp" -e "nr_file" > $output_dir/$FS/rocksdb/Load$workload_id/pg_faults_after_Run$i 2>&1
}

run_workload() {
    workload_id=$1
    i=$2

    cat /proc/vmstat | grep -e "pgfault" -e "pgmajfault" -e "thp" -e "nr_file" 2>&1 > $output_dir/$FS/rocksdb/Run$workload_id/pg_faults_before_Run$i

    echo "run $workload_id"
    time sudo -E numactl --membind=0 ycsb-0.17.0/bin/ycsb run rocksdb -threads $numthreads -P "$HOME/ycsb-0.17.0/workloads/workload$workload_id" -p rocksdb.dir=/mnt/pmem/rocksdb -p recordcount=$record_count -p operationcount=$operation_count > $output_dir/$FS/rocksdb/Run$workload_id/Run$i 2>&1

    cat /proc/vmstat | grep -e "pgfault" -e "pgmajfault" -e "thp" -e "nr_file" 2>&1 > $output_dir/$FS/rocksdb/Run$workload_id/pg_faults_after_Run$i
}

for i in $(seq 5 $iterations)
    do
    if [ $FS = "squirrelfs" ]; then 
        sudo -E insmod ../linux/fs/squirrelfs/squirrelfs.ko; sudo mount -t squirrelfs -o init /dev/pmem0 /mnt/pmem/
    elif [ $FS = "nova" ]; then 
        sudo -E insmod ../linux/fs/nova/nova.ko; sudo mount -t NOVA -o init /dev/pmem0 /mnt/pmem/
    elif [ $FS = "winefs" ]; then 
        sudo -E insmod ../linux/fs/winefs/winefs.ko; sudo mount -t winefs -o init /dev/pmem0 /mnt/pmem/
    elif [ $FS = "ext4" ]; then 
        yes | sudo mkfs.ext4 /dev/pmem0 
        sudo -E mount -t ext4 -o dax /dev/pmem0 /mnt/pmem/
    fi 
    sudo mkdir /mnt/pmem/rocksdb
    mkdir -p $output_dir/$FS/rocksdb/Loada
    mkdir -p $output_dir/$FS/rocksdb/Runa
    mkdir -p $output_dir/$FS/rocksdb/Runb
    mkdir -p $output_dir/$FS/rocksdb/Runc
    mkdir -p $output_dir/$FS/rocksdb/Rund
    mkdir -p $output_dir/$FS/rocksdb/Loade
    mkdir -p $output_dir/$FS/rocksdb/Rune 
    mkdir -p $output_dir/$FS/rocksdb/Runf

    load_workload "a" $i
    run_workload "a" $i 
    run_workload "b" $i 
    run_workload "c" $i 
    run_workload "d" $i 
    load_workload "e" $i 
    run_workload "e" $i 
    run_workload "f" $i 

    sudo umount /dev/pmem0 

    if [ $FS = "squirrelfs" ]; then 
        sudo rmmod squirrelfs
    elif [ $FS = "nova" ]; then 
        sudo rmmod nova 
    elif [ $FS = "winefs" ]; then
        sudo rmmod winefs
    fi

done