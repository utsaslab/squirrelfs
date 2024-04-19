import sys
import csv

versions = ["v3.0", "v4.0", "v5.0", "v6.0"]

def parse_files(fs, runs, csv_writer, result_dir):
    results = {v: {f: [] for f in fs} for v in versions}

    for run in runs:
        for filesystem in fs:
            result_file = result_dir + '/' + filesystem + '/checkout/' + 'Run' + str(run)
            in_file = open(result_file, 'r')
            lines = in_file.readlines()
            current_version = ""
            for line in lines:
                line = line.rstrip("\n").split(",")

                if line[0] in versions:
                    current_version=line[0]
                elif "HEAD" not in line[0]:
                    timing = float(line[0])
                    results[current_version][filesystem].append(timing)

    for v in versions:
        row_header = [v]
        for filesystem in fs:
            row_header.append(filesystem)
        csv_writer.writerow(row_header)

        for i in range(0, len(results[v]["ext4"])):
            row = ['']
            for filesystem in fs:
                row.append(results[v][filesystem][i])
            csv_writer.writerow(row)
        csv_writer.writerow([])
            
    in_file.close()

def main():
    if len(sys.argv) < 6:
        print('Usage: python3 parse_checkout_timing.py <num_fs> <fs1> <fs2> .. <num_runs> <start_run_id> <result_dir> <output_csv_file>')
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

    csv_out_file = open(output_csv_file, "w")
    csv_writer = csv.writer(csv_out_file, delimiter=",")

    parse_files(fs, runs, csv_writer, result_dir)

    csv_out_file.close()

main()