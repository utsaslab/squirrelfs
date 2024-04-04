import csv
import sys

# From https://github.com/utsaslab/WineFS/blob/main/scripts/parse_rocksdb.py

# Usage: python3 parse_rocksdb.py <num_fs> <fs1> <fs2> .. <num_runs> <start_run_id> <result_dir> <output_file>

def parse_file(before_file, after_file, perf_list):
    before_in_file = open(before_file, 'r')
    after_in_file = open(after_file, 'r')
    perf = 0
    before_lines = before_in_file.readlines()
    after_lines = after_in_file.readlines()
    for (before_line, after_line) in zip(before_lines, after_lines):
        before_words = before_line.split()
        if before_words[0] == "pgfault":
            after_words = after_line.split()
            perf = int(after_words[1]) - int(before_words[1])
            perf_list.append(str(perf))
            break

    before_in_file.close()
    after_in_file.close()

def main():
    if len(sys.argv) < 6:
        print('Usage: python3 parse_rocksdb.py <num_fs> <fs1> <fs2> .. <num_runs> <start_run_id> <result_dir> <output_csv_file)')
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

    # workloads = ['Loada', 'Runa', 'Runb', 'Runc', 'Rund', 'Loade', 'Rune', 'Runf']
    # dirs = ['Loada', 'Runa', 'Runb', 'Runc', 'Rund', 'Loade', 'Rune', 'Runf']
    workloads = ['Rune']
    dirs = ['Rune']

    csv_out_file = open(output_csv_file, mode='w')
    csv_writer = csv.writer(csv_out_file, delimiter=',')

    for workload,directory in zip(workloads,dirs):
        rowheader = []
        rowheader.append(workload)
        for filesystem in fs:
            rowheader.append(filesystem)

        csv_writer.writerow(rowheader)

        for run in runs:
            perf_list = []
            perf_list.append('')

            for filesystem in fs:
                before_file = result_dir + '/' + filesystem + '/rocksdb/' + directory + '/' + 'pg_faults_before_Run' + str(run)
                after_file = result_dir + '/' + filesystem + '/rocksdb/' + directory + '/' + 'pg_faults_after_Run' + str(run)
                parse_file(before_file, after_file, perf_list)

            csv_writer.writerow(perf_list)

        csv_writer.writerow([])

if __name__ == '__main__':
    main()