#!/bin/bash 

THREADS=$1
OUTPUT_DIR=$2

if [ -z $THREADS ] | [ -z $OUTPUT_DIR ]; then 
    echo "Usage: run_model_sims.sh threads output_dir"
    exit 1
fi

cd ../model_checking
./javautil.sh Runner.java -s $THREADS
cp output/model_sim_results ../evaluation/$OUTPUT_DIR/squirrelfs