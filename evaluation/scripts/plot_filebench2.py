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
    grouped_min = {k:[sys.maxsize, sys.maxsize, sys.maxsize, sys.maxsize] for k in file_sys}
    grouped_max = {k:[0, 0, 0, 0] for k in file_sys}
    for i in range(len(filebench_run_names)):
        workload = filebench_run_names[i]
        for run in filebench_runs[workload]:
            filebench_runs_avg[workload][0] += run[0]
            if run[0] < grouped_min["Ext4-DAX"][i]:
                grouped_min["Ext4-DAX"][i] = run[0]
            if run[0] > grouped_max["Ext4-DAX"][i]:
                grouped_max["Ext4-DAX"][i] = run[0]

            filebench_runs_avg[workload][1] += run[1]
            if run[1] < grouped_min["NOVA"][i]:
                grouped_min["NOVA"][i] = run[1]
            if run[1] > grouped_max["NOVA"][i]:
                grouped_max["NOVA"][i] = run[1]

            filebench_runs_avg[workload][2] += run[2]
            if run[2] < grouped_min["WineFS"][i]:
                grouped_min["WineFS"][i] = run[2]
            if run[2] > grouped_max["WineFS"][i]:
                grouped_max["WineFS"][i] = run[2]

            filebench_runs_avg[workload][3] += run[3]
            if run[3] < grouped_min["SquirrelFS"][i]:
                grouped_min["SquirrelFS"][i] = run[3]
            if run[3] > grouped_max["SquirrelFS"][i]:
                grouped_max["SquirrelFS"][i] = run[3]

    filebench_grouped_data = {k:[] for k in file_sys}
    filebench_raw_grouped_data = {k:[] for k in file_sys}

    for i in range(len(filebench_run_names)):
        workload = filebench_run_names[i]
        ext4_baseline = (filebench_runs_avg[workload][0] / num_runs) / 1000
        filebench_runs_avg[workload][0] = ((filebench_runs_avg[workload][0] / num_runs) / 1000) 
        filebench_runs_avg[workload][1] = ((filebench_runs_avg[workload][1] / num_runs) / 1000) 
        filebench_runs_avg[workload][2] = ((filebench_runs_avg[workload][2] / num_runs) / 1000) 
        filebench_runs_avg[workload][3] = ((filebench_runs_avg[workload][3] / num_runs) / 1000) 

        filebench_raw_grouped_data["Ext4-DAX"].append(filebench_runs_avg[workload][0])
        filebench_raw_grouped_data["NOVA"].append(filebench_runs_avg[workload][1])
        filebench_raw_grouped_data["WineFS"].append(filebench_runs_avg[workload][2])
        filebench_raw_grouped_data["SquirrelFS"].append(filebench_runs_avg[workload][3])

        filebench_grouped_data["Ext4-DAX"].append(filebench_runs_avg[workload][0] / ext4_baseline)
        filebench_grouped_data["NOVA"].append(filebench_runs_avg[workload][1] / ext4_baseline)
        filebench_grouped_data["WineFS"].append(filebench_runs_avg[workload][2] / ext4_baseline)
        filebench_grouped_data["SquirrelFS"].append(filebench_runs_avg[workload][3] / ext4_baseline)

        grouped_min["Ext4-DAX"][i] = 1 - ((grouped_min["Ext4-DAX"][i] / 1000) / ext4_baseline)
        grouped_max["Ext4-DAX"][i] = ((grouped_max["Ext4-DAX"][i] / 1000) / ext4_baseline) - 1

        grouped_min["NOVA"][i] = filebench_grouped_data["NOVA"][i] - ((grouped_min["NOVA"][i] / 1000) / ext4_baseline)
        grouped_max["NOVA"][i] = ((grouped_max["NOVA"][i] / 1000) / ext4_baseline) - filebench_grouped_data["NOVA"][i]

        grouped_min["WineFS"][i] = filebench_grouped_data["WineFS"][i] - (grouped_min["WineFS"][i] / 1000) / ext4_baseline
        grouped_max["WineFS"][i] = ((grouped_max["WineFS"][i] / 1000) / ext4_baseline) - filebench_grouped_data["WineFS"][i]
        
        
        grouped_min["SquirrelFS"][i] = filebench_grouped_data["SquirrelFS"][i] - (grouped_min["SquirrelFS"][i] / 1000) / ext4_baseline
        grouped_max["SquirrelFS"][i] = ((grouped_max["SquirrelFS"][i] / 1000) / ext4_baseline) - filebench_grouped_data["SquirrelFS"][i]

    print([grouped_min["Ext4-DAX"], grouped_max["Ext4-DAX"]])
    print(filebench_grouped_data["NOVA"])
    print([grouped_min["NOVA"], grouped_max["NOVA"]])

    params = {'legend.fontsize': 'large',
         'axes.labelsize': 'x-large',
         'axes.titlesize':'large',
         'xtick.labelsize':'large',
         'ytick.labelsize':'large'}
    pylab.rcParams.update(params)


    x = np.arange(len(filebench_run_names))
    width = 0.2

    fig, ax = plt.subplots()

    r1 = ax.bar(x-width*1.5, filebench_grouped_data["Ext4-DAX"], width, hatch="//", yerr=[grouped_min["Ext4-DAX"], grouped_max["Ext4-DAX"]], error_kw=dict(ecolor="red"))
    autolabel(ax, r1, filebench_raw_grouped_data["Ext4-DAX"])
    r2 = ax.bar(x-width*0.5, filebench_grouped_data["NOVA"], width, hatch="--", yerr=[grouped_min["NOVA"], grouped_max["NOVA"]], error_kw=dict(ecolor="red"))
    r3 = ax.bar(x+width*0.5, filebench_grouped_data["WineFS"], width, hatch="\\\\", yerr=[grouped_min["WineFS"], grouped_max["WineFS"]], error_kw=dict(ecolor="red"))
    r4 = ax.bar(x+width*1.5, filebench_grouped_data["SquirrelFS"], width, color="black", yerr=[grouped_min["SquirrelFS"], grouped_max["SquirrelFS"]], error_kw=dict(ecolor="red"))
    ax.grid(True, zorder=0)
    ax.set_axisbelow(True)

    plt.xticks(x, filebench_run_names)
    plt.ylabel("Throughput relative to Ext4-DAX (Kops/sec)")
    plt.xlabel("filebench workloads")
    plt.ylim(0,2.25)
    plt.legend(file_sys)

    plt.savefig("graphs/filebench.pdf", format="pdf")

def autolabel(ax, rects, data):
    i = 0
    for rect in rects:
        throughput = data[i]
        ax.annotate("{}".format(int(round(throughput, 0))),
            xy=(rect.get_x() + rect.get_width() / 2, rect.get_height()),
            rotation=90,
            ha="center", va="bottom",
            fontsize=12
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