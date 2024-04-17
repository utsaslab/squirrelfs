#!/bin/python3 

import matplotlib.pyplot as plt
import matplotlib.pylab as pylab
import csv
import numpy as np
import sys

file_sys = ["Ext4-DAX", "NOVA", "WineFS", "SquirrelFS"]
# file_sys_with_arckfs = ["Ext4-DAX", "NOVA", "WineFS", "ArckFS", "SquirrelFS"]
syscall_labels = ["1K\nappend", "16K\nappend", "1K\nread", "16K\nread", "creat", "mkdir", "rename", "unlink"]
rocksdb_labels = ["Load A", "Run A", "Run B", "Run C", "Run D", "Load E", "Run E", "Run F"]
syscalls = ["append_1k", "append_16k", "read_1k", "read_16k", "creat", "mkdir", "rename", "unlink"]
filebench_workloads = ["fileserver", "varmail", "webproxy", "webserver"]
lmdb_workloads = ["fillseqbatch", "fillrandbatch", "fillrandom"]
rocksdb_workloads = ["Loada", "Runa", "Runb", "Runc", "Rund", "Loade", "Rune", "Runf"]

def read_data(results_file, workloads, kops, fs):
    all_data = {f: {w: [] for w in workloads} for f in fs}
    with open(results_file, "r") as f:
        reader = csv.reader(f)
        current_run = ""
        for row in reader:
            if len(row) == 0:
                continue 
            elif row[0] in workloads:
                current_run = row[0]
            elif row[0] == "":
                for i in range(len(fs)):
                    f = fs[i]
                    data = float(row[i+1])
                    if kops:
                        data = data / 1000
                    all_data[f][current_run].append(data)
    return all_data

def read_data_with_error(results_file, workloads, kops, relative, fs):
    all_data = read_data(results_file, workloads, kops, fs)
    avg = {}
    if relative:
        ext4_baselines = {w: np.average(np.array(all_data["Ext4-DAX"][w])) for w in workloads}
        avg = {f: [np.average(np.array(all_data[f][w])) / ext4_baselines[w] for w in workloads] for f in fs}
        mins = {f: [avg[f][i] - (min(all_data[f][workloads[i]]) / ext4_baselines[workloads[i]]) for i in range(len(workloads))] for f in fs}
        maxes = {f: [(max(all_data[f][workloads[i]]) / ext4_baselines[workloads[i]]) - avg[f][i] for i in range(len(workloads))] for f in fs}
    else:
        avg = {f: [np.average(np.array(all_data[f][w])) for w in workloads] for f in fs}
        mins = {f: [avg[f][i] - min(all_data[f][workloads[i]]) for i in range(len(workloads))] for f in fs}
        maxes = {f: [max(all_data[f][workloads[i]]) - avg[f][i] for i in range(len(workloads))] for f in fs}
    
    ext4_raw = [np.average(np.array(all_data["Ext4-DAX"][w])) for w in workloads]

    return ext4_raw, avg, mins, maxes

def read_data_no_error(results_file, workloads, kops, relative):
    all_data = read_data(results_file, workloads, kops)
    avg = {}
    if relative:
        ext4_baselines = {w: np.average(np.array(all_data["Ext4-DAX"][w])) for w in workloads}
        avg = {f: [np.average(np.array(all_data[f][w])) / ext4_baselines[w] for w in workloads] for f in file_sys}
    else:
        avg = {f: [np.average(np.array(all_data[f][w])) for w in workloads] for f in file_sys}

    ext4_raw = [np.average(np.array(all_data["Ext4-DAX"][w])) for w in workloads]

    return ext4_raw, avg

def autolabel(ax, rects, data):
    i = 0
    for rect in rects:
        throughput = data[i]
        ax.annotate("{}".format(int(round(throughput, 0))),
            xy=(rect.get_x() + rect.get_width() / 2, rect.get_height()),
            rotation=90,
            ha="center", va="bottom",
            fontsize=8
            )
        i += 1

def plot_with_error(data, mins, maxes, ext4_raw, ax, workloads, fs):
    x = np.arange(len(workloads))
    width = 0.15
    if "ArckFS" in fs:
        r1 = ax.bar(x-width*2, data["Ext4-DAX"], width, hatch="..", yerr=[mins["Ext4-DAX"], maxes["Ext4-DAX"]], error_kw=dict(ecolor="red"))
        if ext4_raw != None:
            autolabel(ax, r1, ext4_raw)
        ax.bar(x-width*1, data["NOVA"], width, hatch="--", yerr=[mins["NOVA"], maxes["NOVA"]], error_kw=dict(ecolor="red"))
        ax.bar(x, data["WineFS"], width, hatch="\\\\", yerr=[mins["WineFS"], maxes["WineFS"]], error_kw=dict(ecolor="red"))
        ax.bar(x+width*1, data["ArckFS"], width, yerr=[mins["ArckFS"], maxes["ArckFS"]], error_kw=dict(ecolor="red"))
        ax.bar(x+width*2, data["SquirrelFS"], width, color="black", yerr=[mins["SquirrelFS"], maxes["SquirrelFS"]], error_kw=dict(ecolor="red"))
    else:
        r1 = ax.bar(x-width*1.5, data["Ext4-DAX"], width, hatch="..", yerr=[mins["Ext4-DAX"], maxes["Ext4-DAX"]], error_kw=dict(ecolor="red"))
        if ext4_raw != None:
            autolabel(ax, r1, ext4_raw)
        ax.bar(x-width*0.5, data["NOVA"], width, hatch="--", yerr=[mins["NOVA"], maxes["NOVA"]], error_kw=dict(ecolor="red"))
        ax.bar(x+width*0.5, data["WineFS"], width, hatch="\\\\", yerr=[mins["WineFS"], maxes["WineFS"]], error_kw=dict(ecolor="red"))
        ax.bar(x+width*1.5, data["SquirrelFS"], width, color="black", yerr=[mins["SquirrelFS"], maxes["SquirrelFS"]], error_kw=dict(ecolor="red"))
    

def plot_no_error(data, ext4_raw, ax, workloads):
    x = np.arange(len(workloads))
    width = 0.2

    r1 = ax.bar(x-width*1.5, data["Ext4-DAX"], width, hatch="..")
    if ext4_raw != None:
        autolabel(ax, r1, ext4_raw)
    ax.bar(x-width*0.5, data["NOVA"], width, hatch="--")
    ax.bar(x+width*0.5, data["WineFS"], width, hatch="\\\\")
    ax.bar(x+width*1.5, data["SquirrelFS"], width, color="black")



def plot_data(output_file, syscall_latency_data, syscall_mins, syscall_maxes, filebench_data,
    filebench_mins, filebench_maxes, filebench_ext4_raw, lmdb_data, lmdb_mins, lmdb_maxes, 
    lmdb_ext4_raw,rocksdb_data, rocksdb_mins, rocksdb_maxes, rocksdb_ext4_raw):

    params = {'legend.fontsize': 'small',
         'axes.labelsize': 'small',
         'axes.titlesize':'large',
         'xtick.labelsize':'xx-small',
         'ytick.labelsize':'x-small',
         'figure.figsize': (6.4, 4.4)}
    pylab.rcParams.update(params)

    fig = plt.figure()
    fig, axs = plt.subplots(2,2,gridspec_kw={'width_ratios': [1.75, 1]})


    plot_with_error(syscall_latency_data, syscall_mins, syscall_maxes, None, axs[0,0], syscalls, file_sys)
    plot_with_error(filebench_data, filebench_mins, filebench_maxes, filebench_ext4_raw, axs[0,1], filebench_workloads, file_sys)
    plot_with_error(rocksdb_data, rocksdb_mins, rocksdb_maxes, rocksdb_ext4_raw, axs[1,0], rocksdb_workloads, file_sys)
    plot_with_error(lmdb_data, lmdb_mins, lmdb_maxes, lmdb_ext4_raw, axs[1,1], lmdb_workloads, file_sys)

    axs[0,0].set_ylabel("Latency (us)")
    axs[0,0].set_xlabel("(a) System call latency")
    axs[0,0].set_xticks(np.arange(len(syscall_labels)), syscall_labels)
    axs[0,0].grid(True, zorder=0)
    axs[0,0].set_axisbelow(True)
    axs[0,0].tick_params(left = False, bottom = False, pad=0.5)


    axs[0,1].set_ylabel("kops/s (relative)")
    axs[0,1].set_xlabel("(b) Filebench")
    axs[0,1].set_xticks(np.arange(len(filebench_workloads)), filebench_workloads)
    axs[0,1].grid(True, zorder=0)
    axs[0,1].set_axisbelow(True)
    axs[0,1].tick_params(left = False, bottom = False, pad=0.5)

    axs[1,0].set_ylabel("kops/s (relative)")
    axs[1,0].set_xlabel("(c) YCSB workloads on RocksDB")
    axs[1,0].set_xticks(np.arange(len(rocksdb_labels)), rocksdb_labels)
    axs[1,0].grid(True, zorder=0)
    axs[1,0].set_axisbelow(True)
    axs[1,0].tick_params(left = False, bottom = False, pad=0.5)

    axs[1,1].set_ylabel("kops/s (relative)")
    axs[1,1].set_xlabel("(d) LMDB")
    axs[1,1].set_xticks(np.arange(len(lmdb_workloads)), lmdb_workloads)
    axs[1,1].set_ylim(0.5,1.25)
    axs[1,1].grid(True, zorder=0)
    axs[1,1].set_axisbelow(True)
    axs[1,1].tick_params(left = False, bottom = False, pad=0.5)

    plt.figlegend(file_sys_with_arckfs, loc="upper center", ncol=3)
    
    plt.subplots_adjust(left=0.075, right=0.975, top=0.9, bottom=0.1, wspace=0.175, hspace=0.375)
    plt.savefig(output_file, format="pdf")



def main():
    if len(sys.argv) < 6:
        print("Too few arguments")
        exit(1)
    syscall_results_file = sys.argv[1]
    filebench_results_file = sys.argv[2]
    rocksdb_results_file = sys.argv[3]
    lmdb_results_file = sys.argv[4]
    output_file = sys.argv[5]

    syscall_ext4_raw, syscall_latency_data, syscall_mins, syscall_maxes = read_data_with_error(syscall_results_file, syscalls, False, False, file_sys)
    filebench_ext4_raw, filebench_data, filebench_mins, filebench_maxes = read_data_with_error(filebench_results_file, filebench_workloads, True, True, file_sys)
    lmdb_ext4_raw, lmdb_data, lmdb_mins, lmdb_maxes = read_data_with_error(lmdb_results_file, lmdb_workloads, True, True, file_sys)
    rocksdb_ext4_raw, rocksdb_data, rocksdb_mins, rocksdb_maxes = read_data_with_error(rocksdb_results_file, rocksdb_workloads, True, True, file_sys)

    plot_data(
        output_file,
        syscall_latency_data,
        syscall_mins,
        syscall_maxes,
        filebench_data,
        filebench_mins,
        filebench_maxes,
        filebench_ext4_raw,
        lmdb_data,
        lmdb_mins,
        lmdb_maxes,
        lmdb_ext4_raw,
        rocksdb_data,
        rocksdb_mins,
        rocksdb_maxes,
        rocksdb_ext4_raw
    )

main()