#!/bin/bash

sudo -E ./scripts/run_filebench.sh fileserver squirrelfs
sudo -E ./scripts/run_filebench.sh fileserver nova
sudo -E ./scripts/run_filebench.sh fileserver winefs
sudo -E ./scripts/run_filebench.sh fileserver ext4

sudo -E ./scripts/run_filebench.sh varmail squirrelfs
sudo -E ./scripts/run_filebench.sh varmail nova
sudo -E ./scripts/run_filebench.sh varmail winefs
sudo -E ./scripts/run_filebench.sh varmail ext4

sudo -E ./scripts/run_filebench.sh webserver squirrelfs
sudo -E ./scripts/run_filebench.sh webserver nova
sudo -E ./scripts/run_filebench.sh webserver winefs
sudo -E ./scripts/run_filebench.sh webserver ext4

sudo -E ./scripts/run_filebench.sh webproxy squirrelfs
sudo -E ./scripts/run_filebench.sh webproxy nova
sudo -E ./scripts/run_filebench.sh webproxy winefs
sudo -E ./scripts/run_filebench.sh webproxy ext4
