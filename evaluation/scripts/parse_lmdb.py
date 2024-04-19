#!/bin/python

import csv 
import sys 

def parse_file(filename, perf_list):
    in_file = open(filename, 'r')
    perf = 0.0
    skipline = True
    lines = in_file.readlines()
    for line in lines:
        if "ops/sec" in line:
            if skipline:
                skipline = False
                continue 
            else:
                words = line.split()
                perf = perf + float(words[4])

    perf_list.append(str(round(perf,2)))

    in_file.close()


def main():
    if len(sys.argv) < 6:
        print('Usage: python3 parse_lmdb.py <num_fs> <fs1> <fs2> .. <num_runs> <start_run_id> <result_dir> <output_csv_file)')
        return

    args = sys.argv[1:]

    num_fs = int(args[0])
    fs = []
    for i in range(0, num_fs):
        fs.append(args[i+1])

    num_runs = int(args[num_fs+1])
    start_run_id = int(args[num_fs+2])
    result_dir = args[num_fs+3]
    output_csv_file = args[num_fs+4]

    runs = []
    for i in range(start_run_id, start_run_id + num_runs):
        runs.append(i)

    # print('file systems evaluated = ')
    # print(fs)
    # print('number of runs = ' + str(num_runs) + ', start run id = ' + str(start_run_id))
    # print('result directory = ' + result_dir)

    csv_out_file = open(output_csv_file, mode='w')
    csv_writer = csv.writer(csv_out_file, delimiter=',')

    workloads = ["fillseqbatch", "fillrandbatch", "fillrandom"]

    for workload in workloads:
        rowheader = [workload]
        for filesystem in fs:
            rowheader.append(filesystem)

        csv_writer.writerow(rowheader)

        for run in runs:
            perf_list = []
            perf_list.append('')
            for filesystem in fs:
                result_file = result_dir + "/" + filesystem + "/lmdb/" + workload + "/Run" + str(run)
                parse_file(result_file, perf_list)

            csv_writer.writerow(perf_list)

        csv_writer.writerow([])


main()