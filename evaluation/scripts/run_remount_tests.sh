#!/bin/bash 

# sudo -E scripts/remount_timing.sh nova init 
# sudo -E scripts/remount_timing.sh nova files
# sudo -E scripts/remount_timing.sh nova dirs 

# sudo -E scripts/remount_timing.sh squirrelfs init 
# sudo -E scripts/remount_timing.sh squirrelfs files
# sudo -E scripts/remount_timing.sh squirrelfs dirs 

# sudo -E scripts/remount_timing.sh ext4 init 
# sudo -E scripts/remount_timing.sh ext4 files
# sudo -E scripts/remount_timing.sh ext4 dirs 

# sudo -E scripts/remount_timing.sh winefs init 
# sudo -E scripts/remount_timing.sh winefs files
# sudo -E scripts/remount_timing.sh winefs dirs 

# sudo -E scripts/remount_timing.sh squirrelfs init 
sudo -E scripts/remount_timing.sh squirrelfs empty 
sudo -E scripts/remount_timing.sh squirrelfs fill_files 
sudo -E scripts/remount_timing.sh squirrelfs half_files 
sudo -E scripts/remount_timing.sh squirrelfs fill_dirs 
sudo -E scripts/remount_timing.sh squirrelfs half_dirs