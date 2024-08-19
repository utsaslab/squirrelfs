#!/bin/bash

num_syscall_iterations=10
num_filebench_iterations=5
num_rocksdb_iterations=5
num_lmdb_iterations=5
num_remount_iterations=10
num_compilation_iterations=10
num_checkout_iterations=10

output_dir=$1
data_dir=data-ae
results_dir=results-ae
mkdir -p $data_dir
mkdir -p $results_dir

# parse results from each experiment
echo "parsing benchmark results"
python3 scripts/parse_syscall_latency.py 4 ext4 nova winefs squirrelfs $num_syscall_iterations 0 $output_dir $data_dir/syscall_latency.csv
python3 scripts/parse_filebench.py 4 ext4 nova winefs squirrelfs $num_filebench_iterations 1 $output_dir $data_dir/filebench.csv
python3 scripts/parse_rocksdb.py 4 ext4 nova winefs squirrelfs $num_rocksdb_iterations 1 $output_dir $data_dir/rocksdb.csv
python3 scripts/parse_lmdb.py 4 ext4 nova winefs squirrelfs $num_lmdb_iterations 1 $output_dir $data_dir/lmdb.csv

# plot figure 5
echo "plotting figure 5"
python3 scripts/plot_all.py $data_dir/syscall_latency.csv $data_dir/filebench.csv $data_dir/rocksdb.csv $data_dir/lmdb.csv $results_dir/figure5.pdf 

# generate a table of linux checkout times
echo "generating table of linux checkout times"
python3 scripts/parse_checkout_timing.py 4 ext4 nova winefs squirrelfs $num_checkout_iterations 1 $output_dir $data_dir/checkout.csv
python3 scripts/generate_checkout_table.py $data_dir/checkout.csv $results_dir/checkout_timing.txt

# parse remount timing and print as a table
echo "generating table 2"
python3 scripts/parse_remount_timing.py 1 squirrelfs $num_remount_iterations 1 $output_dir $data_dir/remount_timing.csv
python3 scripts/generate_remount_table.py $data_dir/remount_timing.csv $results_dir/remount_timing.txt

# parse compilation timing and print as a table
echo "generating table 3"
python3 scripts/parse_compilation_timing.py 4 ext4 nova winefs squirrelfs $num_compilation_iterations 1 $output_dir $data_dir/compilation.csv
python3 scripts/generate_compilation_table.py $data_dir/compilation.csv $results_dir/compilation.txt

# parse model simulations and check for failures
echo "parsing simulation results"
python3 scripts/check_sim_results.py $output_dir/squirrelfs/model_sim_results > $results_dir/model_results
