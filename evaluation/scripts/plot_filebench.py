#!/bin/python3 

import matplotlib.pyplot as plt
import matplotlib.pylab as pylab
import csv
import numpy as np
import sys

filebench_run_names = ["fileserver", "varmail", "webproxy", "webserver"]
filebench_runs = {"fileserver": [], "varmail": [], "webproxy": [], "webserver": []}
filebench_runs_avg = {"fileserver": [0, 0, 0, 0], "varmail": [0, 0, 0, 0], "webproxy": [0, 0, 0, 0], "webserver": [0, 0, 0, 0]}
file_sys = ["Ext4-DAX", "NOVA", "WineFS", "SquirrelFS"]

def plot(results_file, num_runs):
    with open(results_file, "r") as f: 
        reader = csv.reader(f)
        current_run = ""
        for row in reader:
            if len(row) == 0:
                continue 
            elif row[0] in filebench_runs:
                current_run = row[0]
            elif row[0] == "":
                data = [float(x) for x in row[1:5]]
                filebench_runs[current_run].append(data)

    # we do multiple runs of each filebench workload - average them here
    for workload in filebench_runs:
        for run in filebench_runs[workload]:
            filebench_runs_avg[workload][0] += run[0]
            filebench_runs_avg[workload][1] += run[1]
            filebench_runs_avg[workload][2] += run[2]
            filebench_runs_avg[workload][3] += run[3]
    for workload in filebench_runs_avg:
        filebench_runs_avg[workload][0] = (filebench_runs_avg[workload][0] / num_runs) / 1000
        filebench_runs_avg[workload][1] = (filebench_runs_avg[workload][1] / num_runs) / 1000
        filebench_runs_avg[workload][2] = (filebench_runs_avg[workload][2] / num_runs) / 1000
        filebench_runs_avg[workload][3] = (filebench_runs_avg[workload][3] / num_runs) / 1000

    print("\t", file_sys)
    for workload in filebench_runs_avg:
        print(workload, filebench_runs_avg[workload])

    params = {'xtick.labelsize':'small', 'ytick.labelsize': 'small'}
    pylab.rcParams.update(params)

    # now that we have averages, plot the data
    fig = plt.figure()
    gs = fig.add_gridspec(2, 2, hspace=0.4, wspace=0.25)
    ax = gs.subplots()

    ax[0,0].bar(file_sys,  filebench_runs_avg["fileserver"])
    ax[0,0].set_title("fileserver")
    ax[0,0].set_ylabel("Kops/sec")
    ax[0,1].bar(file_sys,  filebench_runs_avg["varmail"])
    ax[0,1].set_title("varmail")
    ax[0,1].set_ylabel("Kops/sec")
    ax[1,0].bar(file_sys,  filebench_runs_avg["webserver"])
    ax[1,0].set_title("webserver")
    ax[1,0].set_ylabel("Kops/sec")
    ax[1,1].bar(file_sys,  filebench_runs_avg["webproxy"])
    ax[1,1].set_title("webproxy")
    ax[1,1].set_ylabel("Kops/sec")

    plt.savefig("graphs/filebench.pdf", format="pdf")

def main():
    if len(sys.argv) < 3:
        print("Too few arguments")
        exit(1)
    results_file = sys.argv[1]
    num_runs = int(sys.argv[2])

    plot(results_file, num_runs)

main()