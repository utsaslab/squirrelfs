#!/bin/python3 

import matplotlib.pyplot as plt
import matplotlib.pylab as pylab
import csv
import numpy as np
import sys

syscalls = ["append_1k", "append_16k", "read_1k", "read_16k", "creat", "mkdir", "rename", "unlink"]


def parse_file(filename, perf_list):
    in_file = open(filename, 'r')
    lines = in_file.readlines()
    for line in lines:
        words = line.replace(",", "")
        perf_list.append(int(words))

    in_file.close()

def plot_data(syscall, perf_list, output_dir):
    print(syscall, min(perf_list), max(perf_list))
    fig, ax = plt.subplots()

    ax.hist(perf_list, log=True)
    ax.set_xlabel("latency (us)")
    ax.set_ylabel("number of measurements")

    plt.title(syscall)

    plt.savefig(output_dir + "/tail_" + syscall + ".pdf", format="pdf")

def main():
    if len(sys.argv) < 5:
        print("too few arguments")
        exit(1)
    num_runs = int(sys.argv[1])
    start_run_id = int(sys.argv[2])
    results_dir = sys.argv[3]
    output_dir = sys.argv[4]

    for syscall in syscalls:
        perf_list = []
        for i in range(start_run_id, num_runs):
            parse_file(results_dir + "/syscall_latency/" + syscall + "/Run" + str(i), perf_list)
        plot_data(syscall, perf_list, output_dir)

main()