#!/bin/bash 

# sudo -E scripts/remount_timing.sh squirrelfs empty 
# sudo -E scripts/remount_timing.sh squirrelfs fill_files 
# sudo -E scripts/remount_timing.sh squirrelfs half_files 
# sudo -E scripts/remount_timing.sh squirrelfs fill_dirs 
# sudo -E scripts/remount_timing.sh squirrelfs half_dirs

sudo -E scripts/remount_timing.sh squirrelfs init 
sudo -E scripts/remount_timing.sh squirrelfs empty
sudo -E scripts/remount_timing.sh squirrelfs fill_device