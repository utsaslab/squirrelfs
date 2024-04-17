#!/bin/bash

num_syscall_iterations=10
num_filebench_iterations=5
num_rocksdb_iterations=5
num_lmdb_iterations=5

output_dir=output-ae
data_dir=data-ae
results_dir=results-ae
mkdir -p $data_dir
mkdir -p $results_dir

# parse results from each experiment
python3 scripts/parse_syscall_latency.py 4 ext4 nova winefs squirrelfs $num_syscall_iterations 0 $output_dir $data_dir/syscall_latency.csv
python3 scripts/parse_filebench.py 4 ext4 nova winefs squirrelfs $num_filebench_iterations 1 $output_dir $data_dir/filebench.csv
# python3 scripts/parse_rocksdb.py 4 ext4 nova winefs squirrelfs $num_rocksdb_iterations 1 $output_dir $data_dir/rocksdb.csv
python3 scripts/parse_lmdb.py 4 ext4 nova winefs squirrelfs $num_lmdb_iterations 1 $output_dir $data_dir/lmdb.csv

# # plot figure 5
# python3 scripts/plot_all.py $data_dir/syscall_latency.csv $data_dir/filebench.csv $data_dir/rocksdb.csv $data_dir/lmdb.csv $results_dir/figure5.pdf 

# # TODO: parse linux checkout numbers and print them out as something reasonable

# # TODO: parse remount and recovery times and print them out as a table
# # TODO: this script exists but it needs to be tested
python3 scripts/parse_remount_timing.py 1 squirrelfs 10 1 $output_dir $data_dir/remount_timing.csv

# # TODO: parse compilation times and print them out as a table


# # TODO: parse model sim output and indicate whether any simulations failed
# # TODO: test script with full set of results
python3 scripts/check_sim_results.py $output_dir/squirrelfs/model_sim_results > $results_dir/model_results
