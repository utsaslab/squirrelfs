# Artifact evaluation

This document describes how to reproduce the experiments described in the SquirrelFS paper. 

## Table of contents
1. [Evaluation environment](#evaluation-environment)
2. [Running experiments](#running-experiments)
3. [Evaluation of SquirrelFS's main claims](#evaluation-of-squirrelfss-main-claims)

## Evaluation environment

All experiments in the paper were run with the following system configurations:
1. Debian Bookworm
2. Intel processor supporting `clwb`
   1. This can be checked using `lscpu | grep clwb`. SquirrelFS can be run on processors that only have `clflush`/`clflushopt` support, but this may impact performance.
2. 64 cores
3. 128GB Intel Optane DC Persistent Memory
4. 128GB DRAM

We have set up SquirrelFS and all benchmarks on a machine with these configurations for artifact evaluators. We will provide information about how to access this machine to evaluators during the review period. **Running multiple experiments concurrently on this machine will impact their results, so please coordinate with the other reviewers to ensure experiments do not conflict.**

If you would prefer to use a different machine, please follow the setup instructions in [the README](README.md) to compile and install SquirrelFS. If running SquirrelFS on a different machine, please take note of the following:
- SquirrelFS can be run without `clwb` support on processors that have `clflush`/`clflushopt` support, but this may negatively impact performance, as these instructions are slower. Support for these instructions can be checked by running `lscpu`. 
- SquirrelFS can be run on emulated or real PM. We suggest using the provided hardware or another machine with Intel Optane DC PMM; if this is not an option, note that running the experiments on PM emulated with DRAM will have different performance results.

## Running experiments

Scripts to run all experiments and parse and plot their results are included in the `evaluation/` directory.

### System call latency

### Filebench 

### RocksDB

### LMDB

### Linux checkout 

### Compilation

### Mount times

### Model checking

## Evaluation of SquirrelFS's main claims