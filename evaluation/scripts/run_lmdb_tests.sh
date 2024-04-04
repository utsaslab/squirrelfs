#!/bin/bash 

sudo -E scripts/run_lmdb.sh nova fillseqbatch
sudo -E scripts/run_lmdb.sh squirrelfs fillseqbatch
sudo -E scripts/run_lmdb.sh ext4 fillseqbatch
sudo -E scripts/run_lmdb.sh winefs fillseqbatch

sudo -E scripts/run_lmdb.sh nova fillrandom
sudo -E scripts/run_lmdb.sh squirrelfs fillrandom
sudo -E scripts/run_lmdb.sh ext4 fillrandom
sudo -E scripts/run_lmdb.sh winefs fillrandom

sudo -E scripts/run_lmdb.sh nova fillrandbatch
sudo -E scripts/run_lmdb.sh squirrelfs fillrandbatch
sudo -E scripts/run_lmdb.sh ext4 fillrandbatch
sudo -E scripts/run_lmdb.sh winefs fillrandbatch