#!/bin/python3 

import matplotlib.pyplot as plt
import matplotlib.pylab as pylab
import csv
import numpy as np
import sys
import math

file_sys = ["Ext4-DAX", "NOVA", "WineFS", "SquirrelFS"]
labels = ["1K\nappend", "16K\nappend", "1K\nread", "16K\nread", "creat", "mkdir", "rename", "unlink"]
syscalls = ["append_1k", "append_16k", "read_1k", "read_16k", "creat", "mkdir", "rename", "unlink"]
syscall_runs = {s:[] for s in syscalls}
syscall_avgs = {s:[0,0,0,0] for s in syscalls}
per_fs_data = {f:{s:[] for s in syscalls} for f in file_sys}

def plot(results_file, num_runs):
    with open(results_file + ".csv", "r") as f: 
        reader = csv.reader(f)
        current_run = ""
        for row in reader:
            if len(row) == 0:
                continue 
            elif row[0] in syscall_runs:
                current_run = row[0]
            elif row[0] == "":
                data = [float(x) for x in row[1:5]]
                syscall_runs[current_run].append(data)
                per_fs_data["Ext4-DAX"][current_run].append(data[0])
                per_fs_data["NOVA"][current_run].append(data[1])
                per_fs_data["WineFS"][current_run].append(data[2])
                per_fs_data["SquirrelFS"][current_run].append(data[3])
    
    grouped_min = {k:[sys.maxsize for i in range(len(syscalls))] for k in file_sys}
    grouped_max = {k:[0 for i in range(len(syscalls))] for k in file_sys}
    for i in range(len(syscalls)):
        workload = syscalls[i]
        for run in syscall_runs[workload]:
            syscall_avgs[workload][0] += run[0]
            if run[0] < grouped_min["Ext4-DAX"][i]:
                grouped_min["Ext4-DAX"][i] = run[0]
            if run[0] > grouped_max["Ext4-DAX"][i]:
                grouped_max["Ext4-DAX"][i] = run[0]
            syscall_avgs[workload][1] += run[1]
            if run[1] < grouped_min["NOVA"][i]:
                grouped_min["NOVA"][i] = run[1]
            if run[1] > grouped_max["NOVA"][i]:
                grouped_max["NOVA"][i] = run[1]
            syscall_avgs[workload][2] += run[2]
            if run[2] < grouped_min["WineFS"][i]:
                grouped_min["WineFS"][i] = run[2]
            if run[2] > grouped_max["WineFS"][i]:
                grouped_max["WineFS"][i] = run[2]
            syscall_avgs[workload][3] += run[3]
            if run[3] < grouped_min["SquirrelFS"][i]:
                grouped_min["SquirrelFS"][i] = run[3]
            if run[3] > grouped_max["SquirrelFS"][i]:
                grouped_max["SquirrelFS"][i] = run[3]

    print(grouped_min)
    print(grouped_max)

    for i in range(len(syscalls)):
        workload = syscalls[i]
        syscall_avgs[workload][0] = syscall_avgs[workload][0] / num_runs
        syscall_avgs[workload][1] = syscall_avgs[workload][1] / num_runs
        syscall_avgs[workload][2] = syscall_avgs[workload][2] / num_runs
        syscall_avgs[workload][3] = syscall_avgs[workload][3] / num_runs

    print("\t", file_sys)
    for workload in syscall_avgs:
        print(workload, syscall_avgs[workload])

    grouped_data = {k:[] for k in file_sys}

    # for run in syscall_avgs:
    #     data = syscall_avgs[run]
    for i in range(len(syscalls)):
        data = syscall_avgs[syscalls[i]]

        grouped_data["Ext4-DAX"].append(data[0])
        grouped_data["NOVA"].append(data[1])
        grouped_data["WineFS"].append(data[2])
        grouped_data["SquirrelFS"].append(data[3])

        print(grouped_data)
        

        grouped_min["Ext4-DAX"][i] = grouped_data["Ext4-DAX"][i] - grouped_min["Ext4-DAX"][i]
        grouped_max["Ext4-DAX"][i] = grouped_max["Ext4-DAX"][i] - grouped_data["Ext4-DAX"][i]

        grouped_min["NOVA"][i] = grouped_data["NOVA"][i] - grouped_min["NOVA"][i]
        grouped_max["NOVA"][i] = grouped_max["NOVA"][i] - grouped_data["NOVA"][i]

        grouped_min["WineFS"][i] = grouped_data["WineFS"][i] - grouped_min["WineFS"][i]
        grouped_max["WineFS"][i] = grouped_max["WineFS"][i] - grouped_data["WineFS"][i]

        grouped_min["SquirrelFS"][i] = grouped_data["SquirrelFS"][i] - grouped_min["SquirrelFS"][i]
        grouped_max["SquirrelFS"][i] = grouped_max["SquirrelFS"][i] - grouped_data["SquirrelFS"][i]

    params = {'legend.fontsize': 'large',
         'axes.labelsize': 'x-large',
         'axes.titlesize':'large',
         'xtick.labelsize':'large',
         'ytick.labelsize':'large',
         'xtick.labelsize':'small'}
    pylab.rcParams.update(params)

    x = np.arange(len(syscall_runs))

    width = 0.2

    fig, ax = plt.subplots()

    ax.bar(x-width*1.5, grouped_data["Ext4-DAX"], width, hatch="//", yerr=[grouped_min["Ext4-DAX"], grouped_max["Ext4-DAX"]], error_kw=dict(ecolor="red"))
    ax.bar(x-width*0.5, grouped_data["NOVA"], width, hatch="--", yerr=[grouped_min["NOVA"], grouped_max["NOVA"]], error_kw=dict(ecolor="red"))
    ax.bar(x+width*0.5, grouped_data["WineFS"], width, hatch="\\\\",  yerr=[grouped_min["WineFS"], grouped_max["WineFS"]], error_kw=dict(ecolor="red"))
    ax.bar(x+width*1.5, grouped_data["SquirrelFS"], width, color="black", yerr=[grouped_min["SquirrelFS"], grouped_max["SquirrelFS"]], error_kw=dict(ecolor="red"))

    ax.grid(True, zorder=0)
    ax.set_axisbelow(True)

    plt.ylabel("Latency (microseconds)")
    plt.xlabel("System calls")
    plt.legend(file_sys)
    plt.xticks(x, labels)
    plt.gcf().subplots_adjust(bottom=0.15)
    plt.savefig("graphs/syscall_latency.pdf", format="pdf")

def main():
    if len(sys.argv) < 3:
        print("Too few arguments")
        exit(1)
    results_file = sys.argv[1]
    num_runs = int(sys.argv[2])

    plot(results_file, num_runs)

main()