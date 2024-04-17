import csv
import sys
import numpy as np

def parse_file(filename, perf_list, err_list):
    in_file = open(filename, 'r')
    latency_sum = 0
    num_data = 0 
    lines = in_file.readlines()
    for line in lines:
        words = line.replace(",", "")
        latency_sum += int(words)
        err_list.append(int(words))
        num_data += 1

    perf_list.append(str(round(latency_sum/num_data, 2)))

    in_file.close()


def main():
    if len(sys.argv) < 6:
        print('Usage: python3 parse_syscall.py <num_fs> <fs1> <fs2> .. <num_runs> <start_run_id> <result_dir> <output_csv_file>')
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

    print('file systems evaluated = ')
    print(fs)
    print('number of runs = ' + str(num_runs) + ', start run id = ' + str(start_run_id))
    print('result directory = ' + result_dir)

    # syscalls = ["append_1k", "append_16k", "append_64k", "read_1k", "read_16k", "read_64k", "creat", "mkdir", "rename", "unlink"]
    syscalls = ["append_1k", "append_16k", "read_1k", "read_16k", "creat", "mkdir", "rename", "unlink"]

    all_data = {f:{s:[] for s in syscalls} for f in fs}

    csv_out_file = open(output_csv_file, mode='w')
    csv_writer = csv.writer(csv_out_file, delimiter=',')

    for workload in syscalls:
        rowheader = []
        rowheader.append(workload)
        for filesystem in fs:
            rowheader.append(filesystem)

        csv_writer.writerow(rowheader)

        for run in runs:
            perf_list = []
            perf_list.append('')

            for filesystem in fs:
                result_file = result_dir + '/' + filesystem + '/syscall_latency/' + workload + '/' + 'Run' + str(run)
                parse_file(result_file, perf_list, all_data[filesystem][workload])

            csv_writer.writerow(perf_list)

        csv_writer.writerow([]) 

    # # calculate error
    # error_out_file = open(output_csv_file + "_error.csv", mode="w")
    # csv_writer = csv.writer(error_out_file, delimiter=",")
    # for workload in syscalls:
    #     rowheader = []
    #     rowheader.append(workload)
    #     for filesystem in fs:
    #         rowheader.append(filesystem)

    #     csv_writer.writerow(rowheader)

    #     err_list = []
    #     err_list.append('')

    #     for filesystem in fs:
    #         data = np.array(all_data[filesystem][workload])
    #         err = np.std(data) / np.sqrt(np.size(data))
    #         err_list.append(str(err))

    #     csv_writer.writerow(err_list)
    #     csv_writer.writerow([]) 
            

    # print(all_data)

main()