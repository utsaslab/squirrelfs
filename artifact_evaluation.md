# Artifact evaluation

This document describes how to reproduce the experiments described in the SquirrelFS paper. To reproduce the results provided in the paper, please ensure you are on the `artifact_evaluation` branch; SquirrelFS is under continued development, and this branch represents a snapshot of the system at the time the paper was submitted.

## Table of contents
1. [Evaluation environment](#evaluation-environment)
2. [Running experiments and evaluating results](#running-experiments-and-evaluating-results)

## Evaluation environment

All experiments in the paper were run with the following system configurations:
1. Debian Bookworm
2. Intel processor supporting `clwb`
2. 64 cores
3. 128GB Intel Optane DC Persistent Memory
4. 128GB DRAM

We have set up SquirrelFS and all benchmarks on a machine with these configurations for artifact evaluators. We will provide information about how to access this machine to evaluators during the review period. **Running multiple experiments concurrently on this machine will impact their results, so please coordinate with the other reviewers to ensure experiments do not conflict.**

If you would prefer to use a different machine, please follow the setup instructions in [the README](README.md) to compile and install SquirrelFS. If running SquirrelFS on a different machine, please take note of the following:
- SquirrelFS can be run without `clwb` support on processors that have `clflush`/`clflushopt` support, but this may negatively impact performance, as these instructions are slower. Support for these instructions can be checked by running `lscpu`. 
- SquirrelFS can be run on emulated or real PM. We suggest using the provided hardware or another machine with Intel Optane DC PMM; if this is not an option, note that running the experiments on PM emulated with DRAM will have different performance results.

## Running experiments

Scripts to run all experiments and parse and plot their results are included in the `evaluation/` directory. This section provides details on how to run each experiment and how to compare results to those presented in the paper. The raw output of each script will be placed in the `output-ae` folder. Note that experiments **should not be run in parallel**, as this will impact results.

**All of the following commands should be run from within the `evaluation/` directory.**

### Setup

Run `scripts/build_benchmarks.sh` to compile filebench and LMDB. All other experiment scripts use pre-built binaries or compile the required tests.

### Arguments

Each experiment scripts requires some subset of the following arguments:
1. `mount_point`: the location to mount the file system under test. If you are using the provided machine, we suggest using `/mnt/local_ssd/mnt/pmem/`. 
2. `output_dir`: the directory to place all output data in. The same directory can be passed to all experiments; each experiment creates subdirectories to keep results organized.
3. `pm_device`: the path to the PM device file to use. This will generally be `/dev/pmem0`. 

### Running all experiments

**Run `scripts/run_all.sh <mount_point> <output_dir> <pm_device>` to run all experiments on SquirrelFS.** It takes approximately 18 hours to run all of the experiments. Each experiment can also be run separately following the instructions below.

### System call latency (15-20 min)

**To run the system call latency tests on all evaluated file systems, run `sudo -E scripts/run_syscall_latency_tests.sh <mount_point> <output_dir> <pm_device>`.** It takes approximately 15-20 minutes to run all latency tests on all file systems on the provided machine.

To run the syscall latency test on a single file system, run `scripts/run_syscall_latency.sh <fs> <mount_point> <output_dir> <pm_device>`, where `fs` specifies the file system to test (`squirrelfs`, `nova,` `winefs`, or `ext4`).

### Filebench (1.5-2 hours)

**To run all filebench workloads, run `sudo -E scripts/run_filebench_tests.sh <mount_point> <output_dir> <pm_device>`.** It takes approximately 1.5-2 hours to run all filebench workloads on all file systems on the provided machine.

To specify the workload and file system to test, run `sudo -E scripts/run_filebench.sh <mount_point> <workload> <fs> <output_dir> <pm_device>` where `workload` specifies the filebench workload to run (`fileserver`, `varmail`, `webproxy`, or `webserver`) and `fs` specifies the file system to test (`squirrelfs`, `nova,` `winefs`, or `ext4`).

### YCSB workloads on RocksDB (4-4.5 hours)

**To run all YCSB workloads on RocksDB, run `sudo -E scripts/run_rocksdb_tests.sh <mount_point> <output_dir> <pm_device>`.** It takes approximately 4-4.5 hours to run these experiments on all file systems on the provided machine.

To evaluate a specific file system, run `scripts/run_rocksdb.sh <fs> <mount_point> <output_dir> <pm_device>`` where `fs` specifies the file system to test (`squirrelfs`, `nova,` `winefs`, or `ext4`). This script runs all tested YCSB workloads by default, as some YCSB workloads depend on each other, but a subset can be selected by commenting out workloads to skip on lines 63-70. 

### LMDB (~7 hours)

**To run all LMDB workloads, run `sudo -E scripts/run_lmdb_tests.sh <mount_point> <output_dir> <pm_device>`.** In the paper, we ran each experiment 10 times, but this takes 14-15 hours, so the provided script runs 5 iterations to make this experiment more reasonable for artifact evaluators. You modify the number of iterations by editing the value of the `iterations` variable in `scripts/run_lmdb.sh`; note that reducing the number of iterations may introduce more variation.

To specify the workload and file system to test, run `scripts/run_lmdb.sh <mount_point> <workload> <fs> <output_dir> <pm_device>`` where `workload` specifies the LMDB workload to run (`fillseqbatch`, `fillrandbatch`, or `fillrandom`) and `fs` specifies the file system to test (`squirrelfs`, `nova,` `winefs`, or `ext4`).

### Linux checkout (2 hours)

**To run the Linux checkout experiment on all file systems, run `sudo -E scripts/run_linux_checkout.sh <mount_point> <output_dir> <pm_device>`.** It takes approximately 2 hours to run these experiments on all file systems on the provided machine.

To run the experiment on a single file system, run `sudo -E scripts/linux_checkout.sh <fs> <mount_point> <output_dir> <pm_device>`, where `fs` specifies the file system to test (`squirrelfs`, `nova,` `winefs`, or `ext4`).

### Compilation (15 minutes)

**To measure compilation times of all file systems, run `scripts/run_compilation_tests.sh <output_dir>`.** It takes approximately 15 minutes to run these experiments on all file systems on the provided machine.

<!-- To run the experiment on a single file system, run `scripts/compilation.sh <fs>`, where `fs` specifies the file system to test (`squirrelfs`, `nova,` `winefs`, or `ext4`). -->

### Mount times (1 hour)

**To run experiments to measure the mount time of SquirrelFS, run `sudo -E scripts/run_remount_tests.sh <mount_point> <output_dir> <pm_device>`.** It takes approximately 1 hour to run these experiments on SquirrelFS on the provided machine.

**Note**: When filling up the device to measure the remount timing on a full system, the scripts spawn many processes to create files until the device runs out of space and attempting to create or write to a file returns an error. You may see errors indicating that there is no space left on the device when running this experiment -- this is expected.

We only provide mount time measurements for SquirrelFS in the paper, but if you would like to measure them for other file systems, run `sudo -E scripts/remount_timing.sh <fs> <mount_point> <test> <output_dir> <pm_device>`, where `fs` specifies the file system to test (`squirrelfs`, `nova,` `winefs`, or `ext4`) and `test` specifies which experiment to run (`init`, `empty`, or `fill_device`). The script supports several more experiments, including filling the device with only data files or only directories, but we did not include results from these experiments in the paper. Note that the script only supports automatically running post-crash recovery code for SquirrelFS, as SquirrelFS has a mount-time argument (`force_recovery`) to force recovery code to run on a clean unmount. The other file systems do not have mount-time arguments to force crash recovery and have to be manually modified to make this code run if a crash has not occurred.

### Model checking (30 min)

Fully checking the Alloy model of SquirrelFS takes weeks and cannot be done within the artifact evaluation period. Instead, we provide a set of simulations to run on the model that produce example traces of various operations that SquirrelFS supports. 

**To run this set of simulations, run `scripts/run_model_sims.sh <threads> <output_dir>` where `threads` is the number of threads to use to run the simulations in parallel.** To achieve the best results, we suggest using half of the cores in your machine as the number of threads (e.g. for a 64 core machine, use 32 threads), as some simulations are memory-intensive. It takes approximately 30 minutes to run all simulations with 32 cores.

## Evaluating results

We first describe SquirrelFS's key claims, then describe how to generate the tables and figures in the paper after running all experiments.

### SquirrelFS's main results

1. **SquirrelFS achieves similar or better performance to other in-kernel PM file systems on various benchmarks.** We demonstrate this by running several microbenchmarks, macrobenchmarks, and applications on SquirrelFS and on prior PM file systems ext4-DAX, NOVA, and WineFS. Figure 5 in the paper presents these results. 

2. **SquirrelFS obtains statically-checked crash-consistency guarantees quickly.** We demonstrate this by comparing the average single-threaded compile time for each tested file system. The results of this experiment are shown in Table 3 in the paper. 

3. **SquirrelFS trades off simpler ordering rules for higher mount and recovery performance.** A limitation of the current prototype of SquirrelFS is that it must scan the entire PM device at mount (plus additional cleanup work during recovery), which results in slower mount and recovery times than other systems. We present average mount and recovery times for SquirrelFS in Table 2 in the paper.

4. **SquirrelFS's design is correct.** We model-checked SquirrelFS's design to gain confidence that the ordering rules enforced by the compiler provide crash consistency. 

### Comparing results

We provide scripts to generate Figure 5 and Tables 2 and 3 from the paper, and to process other data (Linux checkout times, model checking simulation results) into a readable format.

**To generate all scripts and tables, run `scripts/process_results.sh`.** If you have modified any of the experiment scripts (e.g., output directory, number of iterations), please update this script accordingly.

This script creates a directory `results-ae`, generates the following files, and places them in that directory.
1. `figure5.pdf`: A PDF with bar charts showing latency or throughput for the system call latency, filebench, RocksDB, and LMDB workloads. 
2. 
3. 

Additionally, the script prints out the total number of model simulations run and the number that passed and failed. It will also print the name of any failing simulations. All simulations are expected to pass.
