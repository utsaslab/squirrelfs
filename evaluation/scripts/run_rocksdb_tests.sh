#!/bin/bash

sudo -E scripts/run_rocksdb.sh nova
sudo -E scripts/run_rocksdb.sh squirrelfs 
sudo -E scripts/run_rocksdb.sh ext4
sudo -E scripts/run_rocksdb.sh winefs 