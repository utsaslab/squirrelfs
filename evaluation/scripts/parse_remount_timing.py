import csv
import sys

def parse_file(filename, perf_list):
    in_file = open(filename, 'r')
    lines = in_file.readlines()
    for line in lines:
        line = line.replace("m", " ")
        words = line.split()
        if len(words) > 0 and words[0] == "real":
            # put a space between the minutes and seconds in the timing to 
            # make it easier to parse
            # note that this assumes all mounts take less than 1 minute!
            if len(words) > 0:
                val = float(words[-1][:-1])
                perf_list.append(str(round(val, 2)))

    in_file.close()

def print_averages(result_file, experiments, num_runs):
    ext4_avg = 0
    nova_avg = 0
    winefs_avg = 0
    squirrelfs_avg = 0

    with open(result_file, "r") as in_file:
        csv_reader = csv.reader(in_file, delimiter=',')
        currently_reading = ""
        for row in csv_reader:
            if len(row) == 0 or (len(row) == 1 and row[0] == ""):
                continue
            if row[0] in experiments:
                if currently_reading == "":
                    currently_reading = row[0]
                else:
                    squirrelfs_avg = squirrelfs_avg / num_runs
                    print(currently_reading, str(squirrelfs_avg))
                    squirrelfs_avg = 0
                    currently_reading = row[0]
            elif row[0] == "":
                squirrelfs_avg += float(row[1])

        squirrelfs_avg = squirrelfs_avg / num_runs 
        print(currently_reading, str(squirrelfs_avg))
    
    # # ext4_avg = ext4_avg / num_runs 
    # # nova_avg = nova_avg / num_runs 
    # # winefs_avg = winefs_avg / num_runs 
    

    # print("dirs")
    # # print("ext4-dax avg:", round(ext4_avg, 2), "s")
    # # print("nova avg:", round(nova_avg, 2), "s")
    # # print("winefs avg:", round(winefs_avg, 2), "s")
    # print("squirrelfs avg:", round(squirrelfs_avg, 2), "s")

def main():
    if len(sys.argv) < 6:
        print('Usage: python3 parse_remount_timing.py <num_fs> <fs1> <fs2> .. <num_runs> <start_run_id> <result_dir> <output_csv_file>')
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

    # experiments = ["init", "empty", "fill_files", "half_files", "fill_device"]
    experiments = ["init", "empty", "fill_device"]

    csv_out_file = open(output_csv_file, mode='w')
    csv_writer = csv.writer(csv_out_file, delimiter=',')

    for workload in experiments:
        rowheader = []
        rowheader.append(workload)
        for filesystem in fs:
            rowheader.append(filesystem)

        csv_writer.writerow(rowheader)

        for run in runs:
            perf_list = []
            perf_list.append('')

            for filesystem in fs:
                result_file = result_dir + '/' + filesystem + '/remount_timing/' + workload + '/' + 'Run' + str(run)
                parse_file(result_file, perf_list)

            csv_writer.writerow(perf_list)

        csv_writer.writerow([]) 

    csv_out_file.close()

    print_averages(output_csv_file, experiments, num_runs)

main()