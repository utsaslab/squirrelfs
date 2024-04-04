#!/bin/python3 

import matplotlib.pyplot as plt
import matplotlib.pylab as pylab
import csv
import numpy as np
import sys

rocksdb_run_names = ["Load A", "Run A", "Run B", "Run C", "Run D", "Load E", "Run E", "Run F"]
rocksdb_runs = {"Loada": [], "Runa": [], "Runb": [], "Runc": [], "Rund": [], "Loade": [], "Rune": [], "Runf": []}
file_sys = ["Ext4-DAX", "NOVA", "WineFS", "SquirrelFS"]

def plot_rocksdb(rocksdb_results_file, output_file):
    with open(rocksdb_results_file, "r") as f:
        reader = csv.reader(f)
        current_run = ""
        for row in reader:
            if len(row) == 0:
                continue 
            elif row[0] in rocksdb_runs:
                current_run = row[0]
            elif row[0] == "":
                data = [float(x) for x in row[1:5]]
                rocksdb_runs[current_run] = data

    filesys_grouped_data = {k:[] for k in file_sys}
    filesys_raw_grouped_data = {k:[] for k in file_sys}
    for run in rocksdb_runs:
        data = rocksdb_runs[run]

        ext4_baseline = data[0]
        # calculate throughput wrt ext4 baseline so that everything
        # will fit on the same graph
        nova = data[1] / ext4_baseline
        winefs = data[2] / ext4_baseline 
        squirrelfs = data[3] / ext4_baseline

        filesys_raw_grouped_data["Ext4-DAX"].append(data[0])
        filesys_raw_grouped_data["NOVA"].append(data[1])
        filesys_raw_grouped_data["WineFS"].append(data[2])
        filesys_raw_grouped_data["SquirrelFS"].append(data[3])

        filesys_grouped_data["Ext4-DAX"].append(1)
        filesys_grouped_data["NOVA"].append(nova)
        filesys_grouped_data["WineFS"].append(winefs)
        filesys_grouped_data["SquirrelFS"].append(squirrelfs)

    params = {'legend.fontsize': 'large',
        'axes.labelsize': 'x-large',
        'axes.titlesize':'large',
        # 'xtick.labelsize':'large',
        'ytick.labelsize':'large'}
    pylab.rcParams.update(params)

    x = np.arange(len(rocksdb_run_names))

    width = 0.2

    fig, ax = plt.subplots()

    r1 = ax.bar(x-width*1.5, filesys_grouped_data["Ext4-DAX"], width, hatch="//")
    autolabel(ax, r1, filesys_raw_grouped_data["Ext4-DAX"])
    r2 = ax.bar(x-width*0.5, filesys_grouped_data["NOVA"], width, hatch="--")
    r3 = ax.bar(x+width*0.5, filesys_grouped_data["WineFS"], width, hatch="\\\\")
    r4 = ax.bar(x+width*1.5, filesys_grouped_data["SquirrelFS"], width, color="black")
    ax.grid(True, zorder=0)
    ax.set_axisbelow(True)

    plt.xticks(x, rocksdb_run_names)
    plt.ylabel("Throughput relative to Ext4-DAX (Kops/sec)")
    plt.xlabel("YCSB workload on RocksDB")
    plt.legend(file_sys)
    plt.ylim(0.5,3.5)
    plt.savefig(output_file, format="pdf")

def autolabel(ax, rects, data):
    i = 0
    for rect in rects:
        throughput = data[i]
        ax.annotate("{}".format(int(round(throughput / 1000, 0))),
            xy=(rect.get_x() + rect.get_width() / 2, rect.get_height()),
            rotation=90,
            ha="center", va="bottom",
            fontsize=10
            )
        i += 1

def main():
    if len(sys.argv) < 3:
        print("Too few arguments")
        exit(1)
    rocksdb_results_file = sys.argv[1]
    output_file = sys.argv[2]

    plot_rocksdb(rocksdb_results_file, output_file)


main()