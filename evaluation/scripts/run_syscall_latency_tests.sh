#!/bin/bash

sudo -E scripts/run_syscall_latency.sh nova 
sudo -E scripts/run_syscall_latency.sh squirrelfs
sudo -E scripts/run_syscall_latency.sh ext4
sudo -E scripts/run_syscall_latency.sh winefs
sudo -E scripts/run_syscall_latency.sh arckfs