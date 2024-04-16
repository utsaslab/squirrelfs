#!/bin/bash

sudo -E scripts/run_syscall_latency_tests.sh
sudo -E scripts/run_filebench_tests.sh
sudo -E scripts/run_rocksdb_tests.sh