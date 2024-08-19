#!/bin/python3 

import matplotlib.pyplot as plt
import matplotlib.pylab as pylab
import csv
import numpy as np
import sys

file_sys = ["Ext4-DAX", "NOVA", "WineFS", "SquirrelFS"]
workload_names = ["fillseqbatch", "fillrandbatch", "fillrandom"]

def plot(results_file, num_runs):
    all_data = {f:[[] for w in workload_names] for f in file_sys}
    with open(results_file, "r") as f:
        reader = csv.reader(f)
        current_run = -1
        for row in reader:
            if len(row) == 0:
                continue 
            if row[0] in workload_names:
                current_run += 1
            elif row[0] == "":
                all_data["Ext4-DAX"][current_run].append(float(row[1]) / 1000)
                all_data["NOVA"][current_run].append(float(row[2]) / 1000)
                all_data["WineFS"][current_run].append(float(row[3]) / 1000)
                all_data["SquirrelFS"][current_run].append(float(row[4])/ 1000)

    
    lmdb_avgs = {f: [np.average(np.array(all_data[f][i])) for i in range(len(workload_names))] for f in file_sys}

    mins = {f: [lmdb_avgs[f][i] - min(all_data[f][i]) for i in range(len(workload_names))] for f in file_sys}
    maxes = {f: [max(all_data[f][i]) - lmdb_avgs[f][i] for i in range(len(workload_names))] for f in file_sys}

    params = {'legend.fontsize': 'large',
         'axes.labelsize': 'x-large',
         'axes.titlesize':'large',
         'xtick.labelsize':'large',
         'ytick.labelsize':'large'}
    pylab.rcParams.update(params)

    fig, ax = plt.subplots()
    x = np.arange(len(workload_names))
    width = 0.2
    
    
    r1 = ax.bar(x-width*1.5, lmdb_avgs["Ext4-DAX"], width, hatch="//", yerr=[mins["Ext4-DAX"], maxes["Ext4-DAX"]], error_kw=dict(ecolor="red"))
    r2 = ax.bar(x-width*0.5, lmdb_avgs["NOVA"], width, hatch="--", yerr=[mins["NOVA"], maxes["NOVA"]], error_kw=dict(ecolor="red"))
    r3 = ax.bar(x+width*0.5, lmdb_avgs["WineFS"], width, hatch="\\\\", yerr=[mins["WineFS"], maxes["WineFS"]], error_kw=dict(ecolor="red"))
    r4 = ax.bar(x+width*1.5, lmdb_avgs["SquirrelFS"], width, color="black", yerr=[mins["SquirrelFS"], maxes["SquirrelFS"]], error_kw=dict(ecolor="red"))
    ax.grid(True, zorder=0)
    ax.set_axisbelow(True)

    plt.ylabel("Throughput (Kops/sec)", fontsize=14)
    plt.xlabel("Workloads", fontsize=14)
    plt.xticks(x, workload_names)
    plt.legend(file_sys)

    plt.savefig("graphs/lmdb.pdf", format="pdf")


def autolabel(ax, rects, data):
    i = 0
    for rect in rects:
        throughput = data[i]
        ax.annotate("{}".format(int(round(throughput, 0))),
            xy=(rect.get_x() + rect.get_width() / 2, rect.get_height()),
            rotation=90,
            ha="center", va="bottom"
            )
        i += 1

def main():
    if len(sys.argv) < 3:
        print("Too few arguments")
        exit(1)
    results_file = sys.argv[1]
    num_runs = int(sys.argv[2])

    plot(results_file, num_runs)

main()