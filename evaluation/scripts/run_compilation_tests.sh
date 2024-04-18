#!/bin/bash

# iterations=10
iterations=1

OUTPUT_DIR=$1
if [ -z $OUTPUT_DIR ]; then 
    echo "Usage: run_compliation_tests.sh output_dir"
    exit 1
fi
sudo mkdir -p $OUTPUT_DIR

scripts/compilation.sh nova $OUTPUT_DIR $iterations
scripts/compilation.sh squirrelfs $OUTPUT_DIR $iterations
scripts/compilation.sh ext4 $OUTPUT_DIR $iterations
scripts/compilation.sh winefs $OUTPUT_DIR $iterations