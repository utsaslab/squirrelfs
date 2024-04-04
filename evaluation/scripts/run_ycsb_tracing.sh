#!/bin/bash

sudo scripts/ycsb_tracing.sh nova 1
sudo scripts/ycsb_tracing.sh nova 8

sudo scripts/ycsb_tracing.sh squirrelfs 1
sudo scripts/ycsb_tracing.sh squirrelfs 8

sudo scripts/ycsb_tracing.sh ext4 1
sudo scripts/ycsb_tracing.sh ext4 8

sudo scripts/ycsb_tracing.sh winefs 1
sudo scripts/ycsb_tracing.sh winefs 8
