#!/bin/bash

sudo ./run_test.sh micro_stat rust
sudo ./run_test.sh micro_stat ext4 ordered 
sudo ./run_test.sh micro_stat ext4 writeback 

sudo ./run_test.sh micro_delete rust
sudo ./run_test.sh micro_delete ext4 ordered 
sudo ./run_test.sh micro_delete ext4 writeback 

sudo ./run_test.sh micro_read_write rust
sudo ./run_test.sh micro_read_write ext4 ordered 
sudo ./run_test.sh micro_read_write ext4 writeback 

sudo ./run_test.sh micro_append rust
sudo ./run_test.sh micro_append ext4 ordered 
sudo ./run_test.sh micro_append ext4 writeback 

sudo ./run_test.sh micro_mkdir rust
sudo ./run_test.sh micro_mkdir ext4 ordered 
sudo ./run_test.sh micro_mkdir ext4 writeback 
