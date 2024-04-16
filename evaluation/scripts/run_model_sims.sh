#!/bin/bash 

THREADS=$1

cd ../model_checking
./javautil.sh Runner.java -s $THREADS
cp output/model_sim_results ../evaluation/output-ae/squirrelfs